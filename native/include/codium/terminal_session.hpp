#pragma once

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
    void Stop();
    void HandleProcessExit(long pid, int exitCode);
    bool IsRunning() const { return process_ != nullptr && pid_ != 0; }

    wxArrayString Poll();

private:
    void ReadStream(wxInputStream* stream, std::string& buffer, const wxString& prefix,
                    wxArrayString& lines);

    wxEvtHandler* owner_;
    int processId_;
    wxProcess* process_ = nullptr;
    long pid_ = 0;
    std::string outputBuffer_;
    std::string errorBuffer_;
};

} // namespace codium
