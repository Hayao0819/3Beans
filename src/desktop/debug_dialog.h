// debug_dialog.h - debugger window over core/debug.h, the same inspection the MCP
// server exposes. Non-modal, so it can be watched while the emulator runs.

#pragma once

#include <wx/wx.h>
#include <wx/notebook.h>

class b3Frame;

class DebugDialog: public wxFrame {
public:
    DebugDialog(b3Frame *frame);

private:
    b3Frame *frame;
    wxTimer *timer;
    wxNotebook *tabs;

    wxTextCtrl *memAddr, *memLen, *memText;
    wxChoice *memCpu;
    wxCheckBox *memVirt, *memUnsafe;

    wxChoice *cpuSel;
    wxTextCtrl *cpuText;

    wxTextCtrl *faultLimit, *faultText;
    wxCheckBox *faultFirst;

    wxTextCtrl *traceLimit, *traceText;

    wxStaticText *status;
    wxCheckBox *autoRefresh;

    wxPanel *makeMemory();
    wxPanel *makeCpu();
    wxPanel *makeFaults();
    wxPanel *makeTrace();

    void refreshAll();
    void refresh(wxCommandEvent &event);
    void tick(wxTimerEvent &event);
    void pageChanged(wxBookCtrlEvent &event);
    void pause(wxCommandEvent &event);
    void resume(wxCommandEvent &event);
    void step(wxCommandEvent &event);
    void armTrace(wxCommandEvent &event);
    void close(wxCloseEvent &event);
    wxDECLARE_EVENT_TABLE();
};
