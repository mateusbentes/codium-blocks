#include "codium/terminal_session.hpp"

#include <wx/utils.h>

namespace codium {

TerminalSession::TerminalSession(wxEvtHandler* owner, int processId)
    : owner_(owner), processId_(processId)
{
}

TerminalSession::~TerminalSession()
{
    Stop();
}

bool TerminalSession::Start(const wxString& program, const wxArrayString& arguments,
                            const wxString& workingDirectory, wxString* error)
{
    if (IsRunning()) {
        if (error) *error = wxS("A terminal session is already running.");
        return false;
    }
    if (program.empty()) {
        if (error) *error = wxS("The terminal program is empty.");
        return false;
    }

    wxArrayString argvStrings;
    argvStrings.Add(program);
    for (const auto& argument : arguments) argvStrings.Add(argument);
    std::vector<const wxChar*> argv;
    argv.reserve(argvStrings.GetCount() + 1);
    for (const auto& argument : argvStrings) argv.push_back(argument.wx_str());
    argv.push_back(nullptr);

    process_ = new wxProcess(owner_, processId_);
    process_->Redirect();
    wxExecuteEnv environment;
    environment.cwd = workingDirectory;
    pid_ = wxExecute(argv.data(), wxEXEC_ASYNC, process_, &environment);
    if (pid_ == 0) {
        delete process_;
        process_ = nullptr;
        if (error) *error = wxString::Format(wxS("Could not start terminal program: %s."), program);
        return false;
    }
    outputBuffer_.clear();
    errorBuffer_.clear();
    return true;
}

bool TerminalSession::Write(const wxString& text)
{
    if (!IsRunning() || !process_->GetOutputStream()) return false;
    const wxScopedCharBuffer utf8 = text.utf8_str();
    wxOutputStream* stream = process_->GetOutputStream();
    stream->Write(utf8.data(), utf8.length());
    stream->Sync();
    return stream->LastWrite() == utf8.length() && stream->IsOk();
}

void TerminalSession::Stop()
{
    if (!process_) return;
    if (IsRunning()) wxKill(pid_, wxSIGTERM, nullptr, wxKILL_CHILDREN);
    process_->Detach();
    delete process_;
    process_ = nullptr;
    pid_ = 0;
}

void TerminalSession::HandleProcessExit(long pid, int)
{
    if (!process_ || pid != pid_) return;
    pid_ = 0;
    delete process_;
    process_ = nullptr;
}

void TerminalSession::ReadStream(wxInputStream* stream, std::string& buffer, const wxString& prefix,
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

wxArrayString TerminalSession::Poll()
{
    wxArrayString lines;
    if (!process_) return lines;
    ReadStream(process_->GetInputStream(), outputBuffer_, wxEmptyString, lines);
    ReadStream(process_->GetErrorStream(), errorBuffer_, wxS("[stderr] "), lines);
    if (pid_ != 0 && !wxProcess::Exists(pid_)) {
        pid_ = 0;
        delete process_;
        process_ = nullptr;
    }
    return lines;
}

} // namespace codium
