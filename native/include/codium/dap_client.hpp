#pragma once

#include <wx/process.h>
#include <wx/string.h>

#include <string>

namespace codium {

class DapClient final {
public:
    DapClient(wxEvtHandler* owner, int processId);
    ~DapClient();

    bool Start(const wxString& program, const wxArrayString& arguments,
               const wxString& workingDirectory, wxString* error = nullptr);
    bool SendRequest(const wxString& command, const wxString& argumentsJson = wxS("{}"));
    bool SetBreakpoints(const wxString& sourcePath, const wxArrayInt& lines);
    bool ConfigurationDone();
    bool RequestThreads();
    bool RequestStackTrace(int threadId = 1);
    bool RequestScopes(int frameId);
    bool RequestVariables(int variablesReference);
    bool Evaluate(const wxString& expression, int frameId = 0);
    bool Stop();
    void HandleProcessExit(long pid, int exitCode);
    bool IsRunning() const { return process_ != nullptr && pid_ != 0; }
    int LastRequestSequence() const { return lastRequestSequence_; }

    wxArrayString Poll();
    int NextSequence() const { return nextSequence_; }

private:
    bool WriteMessage(const wxString& json);
    void ParseFrames(wxArrayString& messages);

    wxEvtHandler* owner_;
    int processId_;
    wxProcess* process_ = nullptr;
    long pid_ = 0;
    int nextSequence_ = 1;
    int lastRequestSequence_ = 0;
    std::string inputBuffer_;
};

} // namespace codium
