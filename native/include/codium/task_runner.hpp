#pragma once

#include "codium/project_config.hpp"

#include <wx/process.h>
#include <wx/string.h>

#include <string>
#include <vector>

namespace codium {

class TaskRunner final {
public:
    TaskRunner(wxEvtHandler* owner, int processId);
    ~TaskRunner();

    bool Run(const ProjectTask& task, wxString* error = nullptr);
    void Stop();
    void HandleProcessExit(long pid, int exitCode);
    bool IsRunning() const { return process_ != nullptr && pid_ != 0; }
    int LastExitCode() const { return lastExitCode_; }
    const ProjectTask& CurrentTask() const { return currentTask_; }

    wxArrayString Poll();

private:
    void ReadStream(wxInputStream* stream, std::string& buffer, const wxString& prefix,
                    wxArrayString& lines);

    wxEvtHandler* owner_;
    int processId_;
    wxProcess* process_ = nullptr;
    long pid_ = 0;
    int lastExitCode_ = 0;
    ProjectTask currentTask_;
    std::string outputBuffer_;
    std::string errorBuffer_;
};

} // namespace codium
