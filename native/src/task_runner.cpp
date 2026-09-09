#include "codium/task_runner.hpp"

#include <wx/utils.h>

namespace codium {

TaskRunner::TaskRunner(wxEvtHandler* owner, int processId)
    : owner_(owner), processId_(processId)
{
}

TaskRunner::~TaskRunner()
{
    Stop();
}

bool TaskRunner::Run(const ProjectTask& task, wxString* error)
{
    if (IsRunning()) {
        if (error) *error = wxS("A task is already running.");
        return false;
    }
    if (task.program.empty()) {
        if (error) *error = wxS("The task has no executable program.");
        return false;
    }

    currentTask_ = task;
    outputBuffer_.clear();
    errorBuffer_.clear();
    lastExitCode_ = 0;
    process_ = new wxProcess(owner_, processId_);
    process_->Redirect();

    wxArrayString argvStrings;
    argvStrings.Add(task.program);
    for (const auto& argument : task.arguments) argvStrings.Add(argument);
    std::vector<const wxChar*> argv;
    argv.reserve(argvStrings.GetCount() + 1);
    for (const auto& argument : argvStrings) argv.push_back(argument.wx_str());
    argv.push_back(nullptr);

    wxExecuteEnv environment;
    environment.cwd = task.workingDirectory;
    pid_ = wxExecute(argv.data(), wxEXEC_ASYNC, process_, &environment);
    if (pid_ == 0) {
        delete process_;
        process_ = nullptr;
        if (error) *error = wxString::Format(wxS("Could not start task: %s."), task.name);
        return false;
    }
    return true;
}

void TaskRunner::Stop()
{
    if (!process_) return;
    if (IsRunning()) wxKill(pid_, wxSIGTERM, nullptr, wxKILL_CHILDREN);
    process_->Detach();
    delete process_;
    process_ = nullptr;
    pid_ = 0;
}

void TaskRunner::HandleProcessExit(long pid, int exitCode)
{
    if (!process_ || pid != pid_) return;
    lastExitCode_ = exitCode;
    pid_ = 0;
    delete process_;
    process_ = nullptr;
}

void TaskRunner::ReadStream(wxInputStream* stream, std::string& buffer, const wxString& prefix,
                            wxArrayString& lines)
{
    if (!stream) return;
    char chunk[4096];
    while (stream->CanRead()) {
        stream->Read(chunk, sizeof(chunk));
        const size_t count = stream->LastRead();
        if (count == 0) break;
        buffer.append(chunk, count);
    }

    size_t newline = std::string::npos;
    while ((newline = buffer.find('\n')) != std::string::npos) {
        std::string line = buffer.substr(0, newline);
        buffer.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.Add(prefix + wxString::FromUTF8(line.data(), line.size()));
    }
}

wxArrayString TaskRunner::Poll()
{
    wxArrayString lines;
    if (!process_) return lines;
    ReadStream(process_->GetInputStream(), outputBuffer_, wxEmptyString, lines);
    ReadStream(process_->GetErrorStream(), errorBuffer_, wxS("[stderr] "), lines);
    if (pid_ != 0 && !wxProcess::Exists(pid_)) {
        pid_ = 0;
        lastExitCode_ = 0;
        delete process_;
        process_ = nullptr;
    }
    return lines;
}

} // namespace codium
