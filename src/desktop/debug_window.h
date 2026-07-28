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

// debug_window.h - one window per view over core/debug.h, the same inspection the MCP
// server exposes. Each is independent and non-modal, so any number of them can be up at
// once, including two memory views at different addresses. Run control lives in the
// Debug menu instead, since it acts on the emulator rather than on any one view.

#pragma once

#include <wx/wx.h>

class b3Frame;

enum DebugView {
    VIEW_MEMORY = 0,
    VIEW_CPU,
    VIEW_FAULTS,
    VIEW_TRACE,
    VIEW_IO,
    VIEW_COUNT
};

class DebugWindow: public wxFrame {
public:
    DebugWindow(b3Frame *frame, DebugView view);

private:
    b3Frame *frame;
    DebugView view;
    wxTimer *timer;

    wxTextCtrl *out;
    wxStaticText *status;
    wxCheckBox *autoRefresh;

    // Only the views that need them build these
    wxTextCtrl *addrCtrl = nullptr;
    wxTextCtrl *lenCtrl = nullptr;
    wxChoice *cpuCtrl = nullptr;
    wxCheckBox *virtCtrl = nullptr;
    wxCheckBox *unsafeCtrl = nullptr;
    wxCheckBox *firstCtrl = nullptr;

    wxSizer *makeControls(wxWindow *parent);
    void setText(const wxString &text);
    void update();

    void refresh(wxCommandEvent &event);
    void tick(wxTimerEvent &event);
    void armTrace(wxCommandEvent &event);
    void close(wxCloseEvent &event);
    wxDECLARE_EVENT_TABLE();
};
