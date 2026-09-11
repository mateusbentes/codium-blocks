// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include <wx/arrstr.h>
#include <wx/process.h>
#include <wx/string.h>

#include <string>
#include <vector>

namespace codium {

class TerminalSession final {
public:
    TerminalSession(wxEvtHandler* owner, int processId);
    ~TerminalSession();

    bool Start(const wxString& program, const wxArrayString& arguments,
               const wxString& workingDirectory, wxString* error = nullptr);
    bool Write(const wxString& text);
    bool Resize(int columns, int rows);
    void Stop();
    void HandleProcessExit(long pid, int exitCode);
    bool IsRunning() const;
    wxString BackendName() const;

    // Returns raw UTF-8 terminal bytes, including ANSI escape sequences.
    wxString PollRaw();
    // Returns complete lines for non-visual consumers and tests.
    wxArrayString Poll();

private:
    bool FlushPendingWrite();

#if defined(__WXMSW__)
    friend bool StartConPty(TerminalSession*, const wxString&, const wxArrayString&,
                            const wxString&, wxString*);
#endif
    wxEvtHandler* owner_;
    int processId_;
    wxProcess* process_ = nullptr;
    long pid_ = 0;
    std::string rawBuffer_;
    std::string lineBuffer_;
    std::string pendingWrite_;
    bool usingPty_ = false;
#if !defined(__WXMSW__)
    int masterFd_ = -1;
    int childPid_ = 0;
#else
    void* pseudoConsole_ = nullptr;
    void* childProcess_ = nullptr;
    void* jobObject_ = nullptr;
    void* inputWrite_ = nullptr;
    void* outputRead_ = nullptr;
    bool usingConPty_ = false;
#endif
};

} // namespace codium
