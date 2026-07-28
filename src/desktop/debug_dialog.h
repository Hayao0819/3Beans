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

    wxTextCtrl *ioText;

    wxStaticText *status;
    wxCheckBox *autoRefresh;

    wxPanel *makeMemory();
    wxPanel *makeCpu();
    wxPanel *makeFaults();
    wxPanel *makeTrace();
    wxPanel *makeIo();

    void refreshAll();
    void refresh(wxCommandEvent &event);
    void tick(wxTimerEvent &event);
    void pageChanged(wxBookCtrlEvent &event);
    void pause(wxCommandEvent &event);
    void resume(wxCommandEvent &event);
    void step(wxCommandEvent &event);
    void armTrace(wxCommandEvent &event);
    void powerButton(wxCommandEvent &event);
    void close(wxCloseEvent &event);
    wxDECLARE_EVENT_TABLE();
};
