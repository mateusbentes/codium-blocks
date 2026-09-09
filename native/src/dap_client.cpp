#include "codium/dap_client.hpp"

#include <wx/utils.h>

#include <cctype>
#include <vector>

namespace codium {

namespace {

wxString JsonEscape(const wxString& value)
{
    wxString escaped;
    for (const auto ch : value) {
        switch (static_cast<wchar_t>(ch)) {
        case '\\': escaped += wxS("\\\\"); break;
        case '"': escaped += wxS("\\\""); break;
        case '\n': escaped += wxS("\\n"); break;
        case '\r': escaped += wxS("\\r"); break;
        case '\t': escaped += wxS("\\t"); break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}

} // namespace

DapClient::DapClient(wxEvtHandler* owner, int processId)
    : owner_(owner), processId_(processId)
{
}

DapClient::~DapClient()
{
    Stop();
}

bool DapClient::Start(const wxString& program, const wxArrayString& arguments,
                      const wxString& workingDirectory, wxString* error)
{
    if (IsRunning()) return true;
    if (program.empty()) {
        if (error) *error = wxS("The debug adapter program is empty.");
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
        if (error) *error = wxString::Format(wxS("Could not start debug adapter: %s."), program);
        return false;
    }
    inputBuffer_.clear();
    nextSequence_ = 1;
    return true;
}

bool DapClient::WriteMessage(const wxString& json)
{
    if (!IsRunning() || !process_->GetOutputStream()) return false;
    const wxScopedCharBuffer body = json.utf8_str();
    const wxString header = wxString::Format(wxS("Content-Length: %lu\r\n\r\n"),
                                             static_cast<unsigned long>(body.length()));
    const wxScopedCharBuffer headerUtf8 = header.utf8_str();
    wxOutputStream* output = process_->GetOutputStream();
    output->Write(headerUtf8.data(), headerUtf8.length());
    output->Write(body.data(), body.length());
    output->Sync();
    return output->IsOk();
}

bool DapClient::SendRequest(const wxString& command, const wxString& argumentsJson)
{
    const int sequence = nextSequence_++;
    return WriteMessage(wxString::Format(
        wxS("{\"seq\":%d,\"type\":\"request\",\"command\":\"%s\",\"arguments\":%s}"),
        sequence, JsonEscape(command), argumentsJson));
}

bool DapClient::SetBreakpoints(const wxString& sourcePath, const wxArrayInt& lines)
{
    wxString json = wxS("{\"source\":{\"path\":\"") + JsonEscape(sourcePath) + wxS("\"},\"breakpoints\":[");
    for (size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) json += wxS(",");
        json += wxString::Format(wxS("{\"line\":%d}"), lines[index]);
    }
    json += wxS("]}");
    return SendRequest(wxS("setBreakpoints"), json);
}

bool DapClient::RequestThreads()
{
    return SendRequest(wxS("threads"));
}

bool DapClient::RequestStackTrace(int threadId)
{
    return SendRequest(wxS("stackTrace"), wxString::Format(wxS("{\"threadId\":%d,\"startFrame\":0,\"levels\":50}"), threadId));
}

bool DapClient::RequestScopes(int frameId)
{
    return SendRequest(wxS("scopes"), wxString::Format(wxS("{\"frameId\":%d}"), frameId));
}

bool DapClient::RequestVariables(int variablesReference)
{
    return SendRequest(wxS("variables"), wxString::Format(wxS("{\"variablesReference\":%d}"), variablesReference));
}

bool DapClient::Evaluate(const wxString& expression, int frameId)
{
    return SendRequest(wxS("evaluate"), wxString::Format(wxS("{\"expression\":\"%s\",\"frameId\":%d,\"context\":\"repl\"}"),
                                                           JsonEscape(expression), frameId));
}

bool DapClient::Stop()
{
    if (!process_) return true;
    if (IsRunning()) {
        SendRequest(wxS("disconnect"), wxS("{\"restart\":false,\"terminateDebuggee\":true}"));
        wxKill(pid_, wxSIGTERM, nullptr, wxKILL_CHILDREN);
    }
    process_->Detach();
    delete process_;
    process_ = nullptr;
    pid_ = 0;
    inputBuffer_.clear();
    return true;
}

void DapClient::HandleProcessExit(long pid, int)
{
    if (!process_ || pid != pid_) return;
    pid_ = 0;
    delete process_;
    process_ = nullptr;
}

void DapClient::ParseFrames(wxArrayString& messages)
{
    while (true) {
        const size_t headerEnd = inputBuffer_.find("\r\n\r\n");
        if (headerEnd == std::string::npos) return;
        const std::string headers = inputBuffer_.substr(0, headerEnd);
        const std::string marker = "Content-Length:";
        const size_t markerStart = headers.find(marker);
        if (markerStart == std::string::npos) {
            inputBuffer_.erase(0, headerEnd + 4);
            continue;
        }
        size_t numberStart = markerStart + marker.size();
        while (numberStart < headers.size() && std::isspace(static_cast<unsigned char>(headers[numberStart]))) ++numberStart;
        size_t numberEnd = numberStart;
        while (numberEnd < headers.size() && std::isdigit(static_cast<unsigned char>(headers[numberEnd]))) ++numberEnd;
        const size_t length = std::stoul(headers.substr(numberStart, numberEnd - numberStart));
        const size_t bodyStart = headerEnd + 4;
        if (inputBuffer_.size() < bodyStart + length) return;
        const std::string body = inputBuffer_.substr(bodyStart, length);
        messages.Add(wxString::FromUTF8(body.data(), body.size()));
        inputBuffer_.erase(0, bodyStart + length);
    }
}

wxArrayString DapClient::Poll()
{
    wxArrayString messages;
    if (!process_) return messages;
    wxInputStream* input = process_->GetInputStream();
    char chunk[4096];
    while (input && input->CanRead()) {
        input->Read(chunk, sizeof(chunk));
        const size_t count = input->LastRead();
        if (count == 0) break;
        inputBuffer_.append(chunk, count);
    }
    ParseFrames(messages);
    if (pid_ != 0 && !wxProcess::Exists(pid_)) {
        pid_ = 0;
        delete process_;
        process_ = nullptr;
    }
    return messages;
}

} // namespace codium
