// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/dap_client.hpp"

#include <wx/utils.h>

#include <cctype>
#include <algorithm>
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

bool WriteAll(wxOutputStream* output, const void* data, size_t length)
{
    const auto* bytes = static_cast<const char*>(data);
    size_t written = 0;
    while (written < length) {
        output->Write(bytes + written, length - written);
        const size_t count = output->LastWrite();
        if (count == 0 || !output->IsOk()) return false;
        written += count;
    }
    return true;
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
    lastError_.clear();
    nextSequence_ = 1;
    lastRequestSequence_ = 0;
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
    if (!WriteAll(output, headerUtf8.data(), headerUtf8.length()) ||
        !WriteAll(output, body.data(), body.length())) {
        lastError_ = wxS("Could not write the complete DAP message.");
        return false;
    }
    output->Sync();
    return output->IsOk();
}

bool DapClient::SendRequest(const wxString& command, const wxString& argumentsJson)
{
    const int sequence = nextSequence_++;
    lastRequestSequence_ = sequence;
    return WriteMessage(wxString::Format(
        wxS("{\"seq\":%d,\"type\":\"request\",\"command\":\"%s\",\"arguments\":%s}"),
        sequence, JsonEscape(command), argumentsJson));
}

bool DapClient::SetBreakpoints(const wxString& sourcePath, const wxArrayInt& lines)
{
    std::vector<DapBreakpointRequest> requests;
    requests.reserve(lines.size());
    for (const int line : lines) {
        DapBreakpointRequest request;
        request.line = line;
        requests.push_back(request);
    }
    return SetBreakpoints(sourcePath, requests);
}

bool DapClient::SetBreakpoints(const wxString& sourcePath, const std::vector<DapBreakpointRequest>& breakpoints)
{
    wxString json = wxS("{\"source\":{\"path\":\"") + JsonEscape(sourcePath) + wxS("\"},\"breakpoints\":[");
    for (size_t index = 0; index < breakpoints.size(); ++index) {
        if (index != 0) json += wxS(",");
        const auto& breakpoint = breakpoints[index];
        json += wxString::Format(wxS("{\"line\":%d"), breakpoint.line);
        if (!breakpoint.condition.empty()) {
            json += wxS(",\"condition\":\"") + JsonEscape(breakpoint.condition) + wxS("\"");
        }
        if (!breakpoint.hitCondition.empty()) {
            json += wxS(",\"hitCondition\":\"") + JsonEscape(breakpoint.hitCondition) + wxS("\"");
        }
        if (!breakpoint.logMessage.empty()) {
            json += wxS(",\"logMessage\":\"") + JsonEscape(breakpoint.logMessage) + wxS("\"");
        }
        json += wxS("}");
    }
    json += wxS("]}");
    return SendRequest(wxS("setBreakpoints"), json);
}

bool DapClient::SetFunctionBreakpoints(const std::vector<DapFunctionBreakpointRequest>& breakpoints)
{
    wxString json = wxS("{\"breakpoints\":[");
    for (size_t index = 0; index < breakpoints.size(); ++index) {
        if (index != 0) json += wxS(",");
        const auto& breakpoint = breakpoints[index];
        json += wxS("{\"name\":\"") + JsonEscape(breakpoint.name) + wxS("\"");
        if (!breakpoint.condition.empty()) {
            json += wxS(",\"condition\":\"") + JsonEscape(breakpoint.condition) + wxS("\"");
        }
        if (!breakpoint.hitCondition.empty()) {
            json += wxS(",\"hitCondition\":\"") + JsonEscape(breakpoint.hitCondition) + wxS("\"");
        }
        json += wxS("}");
    }
    json += wxS("]}");
    return SendRequest(wxS("setFunctionBreakpoints"), json);
}

bool DapClient::SetDataBreakpoints(const std::vector<DapDataBreakpointRequest>& breakpoints)
{
    wxString json = wxS("{\"breakpoints\":[");
    for (size_t index = 0; index < breakpoints.size(); ++index) {
        if (index != 0) json += wxS(",");
        const auto& breakpoint = breakpoints[index];
        json += wxS("{\"dataId\":\"") + JsonEscape(breakpoint.dataId) + wxS("\"");
        if (!breakpoint.accessType.empty()) {
            json += wxS(",\"accessType\":\"") + JsonEscape(breakpoint.accessType) + wxS("\"");
        }
        if (!breakpoint.condition.empty()) {
            json += wxS(",\"condition\":\"") + JsonEscape(breakpoint.condition) + wxS("\"");
        }
        if (!breakpoint.hitCondition.empty()) {
            json += wxS(",\"hitCondition\":\"") + JsonEscape(breakpoint.hitCondition) + wxS("\"");
        }
        json += wxS("}");
    }
    json += wxS("]}");
    return SendRequest(wxS("setDataBreakpoints"), json);
}

bool DapClient::ConfigurationDone()
{
    return SendRequest(wxS("configurationDone"));
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
    lastRequestSequence_ = 0;
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
    constexpr size_t kMaximumFrameSize = 16U * 1024U * 1024U;
    constexpr size_t kMaximumHeaderSize = 64U * 1024U;
    const auto protocolError = [this](const wxString& error) {
        lastError_ = error;
        inputBuffer_.clear();
    };
    while (true) {
        const size_t headerEnd = inputBuffer_.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            if (inputBuffer_.size() > kMaximumHeaderSize) {
                protocolError(wxS("DAP header exceeded the maximum size."));
            }
            return;
        }
        if (headerEnd > kMaximumHeaderSize) {
            protocolError(wxS("DAP header exceeded the maximum size."));
            return;
        }
        const std::string headers = inputBuffer_.substr(0, headerEnd);
        std::string normalizedHeaders = headers;
        std::transform(normalizedHeaders.begin(), normalizedHeaders.end(), normalizedHeaders.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        const std::string marker = "content-length:";
        const size_t markerStart = normalizedHeaders.find(marker);
        if (markerStart == std::string::npos) {
            protocolError(wxS("DAP frame did not contain Content-Length."));
            return;
        }
        size_t numberStart = markerStart + marker.size();
        while (numberStart < normalizedHeaders.size() && std::isspace(static_cast<unsigned char>(normalizedHeaders[numberStart]))) ++numberStart;
        size_t numberEnd = numberStart;
        while (numberEnd < normalizedHeaders.size() && std::isdigit(static_cast<unsigned char>(normalizedHeaders[numberEnd]))) ++numberEnd;
        if (numberStart == numberEnd) {
            protocolError(wxS("DAP Content-Length is not a decimal integer."));
            return;
        }
        size_t length = 0;
        bool validLength = true;
        for (size_t index = numberStart; index < numberEnd; ++index) {
            const size_t digit = static_cast<size_t>(normalizedHeaders[index] - '0');
            if (length > (kMaximumFrameSize - digit) / 10U) {
                validLength = false;
                break;
            }
            length = length * 10U + digit;
        }
        if (!validLength || length > kMaximumFrameSize) {
            protocolError(wxS("DAP Content-Length exceeds the maximum frame size."));
            return;
        }
        const size_t bodyStart = headerEnd + 4;
        if (bodyStart > inputBuffer_.size() || length > inputBuffer_.size() - bodyStart) return;
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
