/*
    Copyright 2026 Hayao0819

    This file is part of 3Beans.

    3Beans is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    3Beans is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with 3Beans. If not, see <https://www.gnu.org/licenses/>.
*/

#include "debug_window.h"
#include "b3_frame.h"
#include "../core/debug.h"

enum WindowEvent {
    REFRESH = 1,
    ARM_TRACE,
    TICK
};

wxBEGIN_EVENT_TABLE(DebugWindow, wxFrame)
EVT_BUTTON(REFRESH, DebugWindow::refresh)
EVT_BUTTON(ARM_TRACE, DebugWindow::armTrace)
EVT_TIMER(TICK, DebugWindow::tick)
EVT_CLOSE(DebugWindow::close)
wxEND_EVENT_TABLE()

static const struct { const char *title; int width, height; } views[VIEW_COUNT] = {
    { "Memory", 580, 400 },
    { "CPU", 380, 330 },
    { "Faults", 520, 400 },
    { "Trace", 400, 400 },
    { "MCU & GPIO", 420, 400 }
};

DebugWindow::DebugWindow(b3Frame *frame, DebugView view):
        wxFrame(nullptr, wxID_ANY, views[view].title, wxDefaultPosition,
            wxSize(views[view].width, views[view].height)), frame(frame), view(view) {
    wxPanel *panel = new wxPanel(this);

    out = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    out->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));

    status = new wxStaticText(panel, wxID_ANY, "");
    autoRefresh = new wxCheckBox(panel, wxID_ANY, "Auto");

    wxBoxSizer *bar = new wxBoxSizer(wxHORIZONTAL);
    bar->Add(status, 1, wxALIGN_CENTER_VERTICAL);
    bar->Add(autoRefresh, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 6);
    bar->Add(new wxButton(panel, REFRESH, "Refresh"), 0, wxLEFT, 4);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    if (wxSizer *controls = makeControls(panel))
        sizer->Add(controls, 0, wxEXPAND | wxALL, 4);
    sizer->Add(out, 1, wxEXPAND | wxLEFT | wxRIGHT, 4);
    sizer->Add(bar, 0, wxEXPAND | wxALL, 4);
    panel->SetSizer(sizer);

    timer = new wxTimer(this, TICK);
    timer->Start(500);
    update();
}

wxSizer *DebugWindow::makeControls(wxWindow *parent) {
    wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
    switch (view) {
    case VIEW_MEMORY:
        addrCtrl = new wxTextCtrl(parent, wxID_ANY, "0x20008000", wxDefaultPosition, wxSize(110, -1));
        lenCtrl = new wxTextCtrl(parent, wxID_ANY, "256", wxDefaultPosition, wxSize(60, -1));
        cpuCtrl = new wxChoice(parent, wxID_ANY);
        virtCtrl = new wxCheckBox(parent, wxID_ANY, "Virtual");
        unsafeCtrl = new wxCheckBox(parent, wxID_ANY, "MMIO");
        row->Add(new wxStaticText(parent, wxID_ANY, "Address"), 0, wxALIGN_CENTER_VERTICAL);
        row->Add(addrCtrl, 0, wxLEFT, 4);
        row->Add(new wxStaticText(parent, wxID_ANY, "Bytes"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
        row->Add(lenCtrl, 0, wxLEFT, 4);
        row->Add(cpuCtrl, 0, wxLEFT, 8);
        row->Add(virtCtrl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
        row->Add(unsafeCtrl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
        break;

    case VIEW_CPU:
        cpuCtrl = new wxChoice(parent, wxID_ANY);
        row->Add(cpuCtrl, 0);
        break;

    case VIEW_FAULTS:
        lenCtrl = new wxTextCtrl(parent, wxID_ANY, "40", wxDefaultPosition, wxSize(60, -1));
        firstCtrl = new wxCheckBox(parent, wxID_ANY, "Earliest");
        row->Add(new wxStaticText(parent, wxID_ANY, "Show"), 0, wxALIGN_CENTER_VERTICAL);
        row->Add(lenCtrl, 0, wxLEFT, 4);
        row->Add(firstCtrl, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
        break;

    case VIEW_TRACE:
        lenCtrl = new wxTextCtrl(parent, wxID_ANY, "64", wxDefaultPosition, wxSize(60, -1));
        row->Add(new wxButton(parent, ARM_TRACE, "Arm"), 0);
        row->Add(new wxStaticText(parent, wxID_ANY, "Show"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
        row->Add(lenCtrl, 0, wxLEFT, 4);
        break;

    default:
        return nullptr;
    }

    if (cpuCtrl) {
        for (int i = 0; i < MAX_CPUS; i++)
            cpuCtrl->Append(Debug::cpuName(i));
        cpuCtrl->SetSelection(0);
    }
    return row;
}

void DebugWindow::update() {
    // The trace ring outlives the core, so it can be read with nothing booted
    if (view == VIEW_TRACE) {
        out->ChangeValue(Debug::traceList(wxAtoi(lenCtrl->GetValue())));
        return;
    }

    std::lock_guard<std::mutex> lock(frame->mutex);
    Core *core = frame->core;
    if (!core) {
        status->SetLabel("no core booted");
        return;
    }

    wxString state = frame->running.load() ? "running" : "stopped";
    if (frame->dbgPause.load()) state += " (broken into)";
    status->SetLabel(wxString::Format("%s  fps=%d  %s", state, core->fps,
        core->n3dsMode ? "New3DS" : "Old3DS"));

    switch (view) {
    case VIEW_MEMORY: {
        long addr = 0, len = 0;
        addrCtrl->GetValue().ToLong(&addr, 0);
        lenCtrl->GetValue().ToLong(&len, 0);
        if (len <= 0 || len > 0x10000) len = 256;
        int cpu = cpuCtrl->GetSelection();
        if (unsafeCtrl->IsChecked() && !virtCtrl->IsChecked()) {
            out->ChangeValue(Debug::rawDump(core, cpu, addr, len));
            break;
        }
        std::string error;
        std::string dump = Debug::hexDump(core, cpu, addr, len, virtCtrl->IsChecked(), error);
        out->ChangeValue(error.empty() ? dump : error + "\n" + dump);
        break;
    }

    case VIEW_CPU: {
        int cpu = cpuCtrl->GetSelection();
        Debug::CpuState s = Debug::cpuState(core, cpu);
        if (!s.valid) {
            out->ChangeValue("cpu not initialized\n");
            break;
        }
        wxString text;
        for (int i = 0; i < 16; i++)
            text += wxString::Format("r%-2d=%08x%s", i, s.regs[i], (i % 4 == 3) ? "\n" : " ");
        text += wxString::Format("cpsr=%08x  mode=%02x %s  halted=%d\n\n",
            s.cpsr, s.cpsr & 0x1F, (s.cpsr & BIT(5)) ? "THUMB" : "ARM", s.halted);
        for (const auto &r : Debug::cp15Regs(core, cpu))
            text += wxString::Format("%-5s = %08x\n", r.name, r.value);
        out->ChangeValue(text);
        break;
    }

    case VIEW_FAULTS:
        out->ChangeValue(Debug::faultList(core, wxAtoi(lenCtrl->GetValue()),
            firstCtrl->IsChecked()));
        break;

    case VIEW_IO:
        out->ChangeValue(Debug::mcuState(core) + "\n" + Debug::gpioState(core));
        break;

    default:
        break;
    }
}

void DebugWindow::refresh(wxCommandEvent &event) {
    update();
}

void DebugWindow::tick(wxTimerEvent &event) {
    if (autoRefresh->IsChecked()) update();
}

void DebugWindow::armTrace(wxCommandEvent &event) {
    Debug::traceArm();
    out->ChangeValue("trace armed (records ARM11A, freezes on null-page entry)\n");
}

void DebugWindow::close(wxCloseEvent &event) {
    timer->Stop();
    Destroy();
}
