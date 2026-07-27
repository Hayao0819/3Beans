#include "debug_dialog.h"
#include "b3_frame.h"
#include "../core/debug.h"

enum DebugEvent {
    REFRESH = 1,
    PAUSE,
    RESUME,
    STEP,
    ARM_TRACE,
    TICK
};

wxBEGIN_EVENT_TABLE(DebugDialog, wxFrame)
EVT_BUTTON(REFRESH, DebugDialog::refresh)
EVT_BUTTON(PAUSE, DebugDialog::pause)
EVT_BUTTON(RESUME, DebugDialog::resume)
EVT_BUTTON(STEP, DebugDialog::step)
EVT_BUTTON(ARM_TRACE, DebugDialog::armTrace)
EVT_TIMER(TICK, DebugDialog::tick)
EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, DebugDialog::pageChanged)
EVT_CLOSE(DebugDialog::close)
wxEND_EVENT_TABLE()

static wxTextCtrl *makeOutput(wxWindow *parent) {
    wxTextCtrl *text = new wxTextCtrl(parent, wxID_ANY, "", wxDefaultPosition, wxSize(560, 340),
        wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    text->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    return text;
}

static void addCpus(wxChoice *choice) {
    for (int i = 0; i < MAX_CPUS; i++)
        choice->Append(Debug::cpuName(i));
    choice->SetSelection(0);
}

DebugDialog::DebugDialog(b3Frame *frame): wxFrame(nullptr, wxID_ANY, "Debugger",
        wxDefaultPosition, wxSize(600, 480)), frame(frame) {
    tabs = new wxNotebook(this, wxID_ANY);
    tabs->AddPage(makeMemory(), "Memory");
    tabs->AddPage(makeCpu(), "CPU");
    tabs->AddPage(makeFaults(), "Faults");
    tabs->AddPage(makeTrace(), "Trace");

    wxBoxSizer *bar = new wxBoxSizer(wxHORIZONTAL);
    status = new wxStaticText(this, wxID_ANY, "");
    autoRefresh = new wxCheckBox(this, wxID_ANY, "Auto");
    bar->Add(status, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
    bar->Add(new wxButton(this, PAUSE, "Pause"), 0, wxLEFT, 2);
    bar->Add(new wxButton(this, RESUME, "Resume"), 0, wxLEFT, 2);
    bar->Add(new wxButton(this, STEP, "Step"), 0, wxLEFT, 2);
    bar->Add(autoRefresh, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
    bar->Add(new wxButton(this, REFRESH, "Refresh"), 0, wxLEFT | wxRIGHT, 2);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(tabs, 1, wxEXPAND | wxALL, 4);
    sizer->Add(bar, 0, wxEXPAND | wxBOTTOM, 4);
    SetSizer(sizer);

    timer = new wxTimer(this, TICK);
    timer->Start(500);
    refreshAll();
}

wxPanel *DebugDialog::makeMemory() {
    wxPanel *panel = new wxPanel(tabs);
    memAddr = new wxTextCtrl(panel, wxID_ANY, "0x20008000", wxDefaultPosition, wxSize(110, -1));
    memLen = new wxTextCtrl(panel, wxID_ANY, "256", wxDefaultPosition, wxSize(60, -1));
    memCpu = new wxChoice(panel, wxID_ANY);
    addCpus(memCpu);
    memVirt = new wxCheckBox(panel, wxID_ANY, "Virtual");
    memUnsafe = new wxCheckBox(panel, wxID_ANY, "MMIO");
    memText = makeOutput(panel);

    wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, "Address"), 0, wxALIGN_CENTER_VERTICAL);
    row->Add(memAddr, 0, wxLEFT, 4);
    row->Add(new wxStaticText(panel, wxID_ANY, "Bytes"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    row->Add(memLen, 0, wxLEFT, 4);
    row->Add(memCpu, 0, wxLEFT, 8);
    row->Add(memVirt, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    row->Add(memUnsafe, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(row, 0, wxALL, 4);
    sizer->Add(memText, 1, wxEXPAND | wxALL, 4);
    panel->SetSizer(sizer);
    return panel;
}

wxPanel *DebugDialog::makeCpu() {
    wxPanel *panel = new wxPanel(tabs);
    cpuSel = new wxChoice(panel, wxID_ANY);
    addCpus(cpuSel);
    cpuText = makeOutput(panel);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(cpuSel, 0, wxALL, 4);
    sizer->Add(cpuText, 1, wxEXPAND | wxALL, 4);
    panel->SetSizer(sizer);
    return panel;
}

wxPanel *DebugDialog::makeFaults() {
    wxPanel *panel = new wxPanel(tabs);
    faultLimit = new wxTextCtrl(panel, wxID_ANY, "40", wxDefaultPosition, wxSize(60, -1));
    faultFirst = new wxCheckBox(panel, wxID_ANY, "Earliest");
    faultText = makeOutput(panel);

    wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(panel, wxID_ANY, "Show"), 0, wxALIGN_CENTER_VERTICAL);
    row->Add(faultLimit, 0, wxLEFT, 4);
    row->Add(faultFirst, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(row, 0, wxALL, 4);
    sizer->Add(faultText, 1, wxEXPAND | wxALL, 4);
    panel->SetSizer(sizer);
    return panel;
}

wxPanel *DebugDialog::makeTrace() {
    wxPanel *panel = new wxPanel(tabs);
    traceLimit = new wxTextCtrl(panel, wxID_ANY, "64", wxDefaultPosition, wxSize(60, -1));
    traceText = makeOutput(panel);

    wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxButton(panel, ARM_TRACE, "Arm"), 0);
    row->Add(new wxStaticText(panel, wxID_ANY, "Show"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    row->Add(traceLimit, 0, wxLEFT, 4);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(row, 0, wxALL, 4);
    sizer->Add(traceText, 1, wxEXPAND | wxALL, 4);
    panel->SetSizer(sizer);
    return panel;
}

void DebugDialog::refreshAll() {
    // The trace rings survive the core, so they are readable even with nothing booted
    int page = tabs->GetSelection();
    if (page == 3) {
        traceText->ChangeValue(Debug::traceList(wxAtoi(traceLimit->GetValue())));
        return;
    }

    std::lock_guard<std::mutex> lock(frame->mutex);
    Core *core = frame->core;
    if (!core) {
        status->SetLabel("no core booted");
        return;
    }

    wxString state = frame->running.load() ? "running" : "stopped";
    if (frame->dbgPause.load()) state += " (paused)";
    status->SetLabel(wxString::Format("%s  fps=%d  %s", state, core->fps,
        core->n3dsMode ? "New3DS" : "Old3DS"));

    switch (page) {
    case 0: {
        long addr = 0, len = 0;
        memAddr->GetValue().ToLong(&addr, 0);
        memLen->GetValue().ToLong(&len, 0);
        if (len <= 0 || len > 0x10000) len = 256;
        int cpu = memCpu->GetSelection();
        if (memUnsafe->IsChecked() && !memVirt->IsChecked()) {
            memText->ChangeValue(Debug::rawDump(core, cpu, addr, len));
            break;
        }
        std::string error;
        std::string out = Debug::hexDump(core, cpu, addr, len, memVirt->IsChecked(), error);
        memText->ChangeValue(error.empty() ? out : error + "\n" + out);
        break;
    }
    case 1: {
        int cpu = cpuSel->GetSelection();
        Debug::CpuState s = Debug::cpuState(core, cpu);
        if (!s.valid) {
            cpuText->ChangeValue("cpu not initialized\n");
            break;
        }
        wxString out;
        for (int i = 0; i < 16; i++)
            out += wxString::Format("r%-2d=%08x%s", i, s.regs[i], (i % 4 == 3) ? "\n" : " ");
        out += wxString::Format("cpsr=%08x  mode=%02x %s  halted=%d\n\n",
            s.cpsr, s.cpsr & 0x1F, (s.cpsr & BIT(5)) ? "THUMB" : "ARM", s.halted);
        for (const auto &r : Debug::cp15Regs(core, cpu))
            out += wxString::Format("%-5s = %08x\n", r.name, r.value);
        cpuText->ChangeValue(out);
        break;
    }
    case 2:
        faultText->ChangeValue(Debug::faultList(core, wxAtoi(faultLimit->GetValue()),
            faultFirst->IsChecked()));
        break;
    }
}

void DebugDialog::refresh(wxCommandEvent &event) {
    refreshAll();
}

void DebugDialog::pageChanged(wxBookCtrlEvent &event) {
    refreshAll();
}

void DebugDialog::tick(wxTimerEvent &event) {
    if (autoRefresh->IsChecked()) refreshAll();
}

void DebugDialog::pause(wxCommandEvent &event) {
    frame->dbgPause.store(true);
    refreshAll();
}

void DebugDialog::resume(wxCommandEvent &event) {
    frame->dbgPause.store(false);
    refreshAll();
}

void DebugDialog::step(wxCommandEvent &event) {
    if (!frame->dbgPause.load()) return;
    frame->dbgStep.store(1);
    refreshAll();
}

void DebugDialog::armTrace(wxCommandEvent &event) {
    Debug::traceArm();
    traceText->ChangeValue("trace armed (records ARM11A, freezes on null-page entry)\n");
}

void DebugDialog::close(wxCloseEvent &event) {
    timer->Stop();
    Destroy();
}
