#include "codium/codeblocks_adapter_client.hpp"

#include <wx/utils.h>

namespace codium {
namespace {

wxString JsonEscape(const wxString& value)
{
    wxString escaped;
    escaped.reserve(value.length() + 8);
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

wxString JsonStringField(const wxString& line, const wxString& field)
{
    const wxString marker = wxString::Format(wxS("\"%s\":\""), field);
    const int start = line.Find(marker);
    if (start == wxNOT_FOUND) return wxEmptyString;
    const int valueStart = start + static_cast<int>(marker.length());
    int valueEnd = valueStart;
    bool escaped = false;
    while (valueEnd < static_cast<int>(line.length())) {
        const wxChar ch = line[valueEnd];
        if (ch == wxChar('"') && !escaped) break;
        escaped = ch == wxChar('\\') && !escaped;
        if (ch != wxChar('\\')) escaped = false;
        ++valueEnd;
    }
    const wxString raw = line.Mid(valueStart, valueEnd - valueStart);
    wxString decoded;
    for (size_t index = 0; index < raw.length(); ++index) {
        if (raw[index] == wxChar('\\') && index + 1 < raw.length()) {
            const wxChar escapedCharacter = raw[++index];
            if (escapedCharacter == wxChar('n')) decoded += wxChar('\n');
            else if (escapedCharacter == wxChar('r')) decoded += wxChar('\r');
            else if (escapedCharacter == wxChar('t')) decoded += wxChar('\t');
            else decoded += escapedCharacter;
        } else {
            decoded += raw[index];
        }
    }
    return decoded;
}

int JsonIntField(const wxString& line, const wxString& field, int fallback = 0)
{
    const wxString marker = wxString::Format(wxS("\"%s\":"), field);
    const int start = line.Find(marker);
    if (start == wxNOT_FOUND) return fallback;
    int index = start + static_cast<int>(marker.length());
    while (index < static_cast<int>(line.length()) &&
           (line[index] == wxChar(' ') || line[index] == wxChar('\t'))) ++index;
    int value = 0;
    bool found = false;
    while (index < static_cast<int>(line.length()) && line[index] >= wxChar('0') && line[index] <= wxChar('9')) {
        value = value * 10 + static_cast<int>(line[index] - wxChar('0'));
        found = true;
        ++index;
    }
    return found ? value : fallback;
}

bool JsonTrueField(const wxString& line, const wxString& field)
{
    return line.Find(wxString::Format(wxS("\"%s\":true"), field)) != wxNOT_FOUND;
}

wxArrayString JsonStringArrayField(const wxString& line, const wxString& field)
{
    wxArrayString values;
    const wxString marker = wxString::Format(wxS("\"%s\":["), field);
    const int start = line.Find(marker);
    if (start == wxNOT_FOUND) return values;
    const int end = static_cast<int>(line.find(wxChar(']'), static_cast<size_t>(start + marker.length())));
    if (end == wxNOT_FOUND) return values;
    const wxString array = line.Mid(start + static_cast<int>(marker.length()), end - start - static_cast<int>(marker.length()));
    int cursor = 0;
    while (cursor < static_cast<int>(array.length())) {
        const int quoteStart = static_cast<int>(array.find(wxChar('"'), static_cast<size_t>(cursor)));
        if (quoteStart == wxNOT_FOUND) break;
        int quoteEnd = quoteStart + 1;
        while (quoteEnd < static_cast<int>(array.length())) {
            if (array[quoteEnd] == wxChar('"') && array[quoteEnd - 1] != wxChar('\\')) break;
            ++quoteEnd;
        }
        if (quoteEnd >= static_cast<int>(array.length())) break;
        values.Add(array.Mid(quoteStart + 1, quoteEnd - quoteStart - 1));
        cursor = quoteEnd + 1;
    }
    return values;
}

CodeBlocksEventKind EventKindFromName(const wxString& name)
{
    if (name == wxS("projectOpened")) return CodeBlocksEventKind::ProjectOpened;
    if (name == wxS("projectClosed")) return CodeBlocksEventKind::ProjectClosed;
    if (name == wxS("buildStarted")) return CodeBlocksEventKind::BuildStarted;
    if (name == wxS("buildFinished")) return CodeBlocksEventKind::BuildFinished;
    if (name == wxS("compilerDiagnostic")) return CodeBlocksEventKind::CompilerDiagnostic;
    if (name == wxS("debugSessionStarted")) return CodeBlocksEventKind::DebugSessionStarted;
    if (name == wxS("debugSessionStopped")) return CodeBlocksEventKind::DebugSessionStopped;
    return CodeBlocksEventKind::PluginCommand;
}

} // namespace

CodeBlocksAdapterClient::CodeBlocksAdapterClient(wxWindow* owner)
    : owner_(owner)
{
}

CodeBlocksAdapterClient::~CodeBlocksAdapterClient()
{
    Stop();
}

bool CodeBlocksAdapterClient::Start(const wxString& executable,
                                    const wxArrayString& arguments,
                                    const wxString& workingDirectory,
                                    const CodeBlocksHostConfiguration& configuration,
                                    wxString* error)
{
    if (IsRunning()) return true;
    if (executable.empty()) {
        if (error) *error = wxS("Code::Blocks adapter executable is empty.");
        return false;
    }

    process_ = new wxProcess(owner_);
    process_->Redirect();
    wxArrayString argvStrings;
    argvStrings.Add(executable);
    for (const auto& argument : arguments) argvStrings.Add(argument);
    std::vector<const wxChar*> argv;
    argv.reserve(argvStrings.GetCount() + 1);
    for (const auto& argument : argvStrings) argv.push_back(argument.wx_str());
    argv.push_back(nullptr);
    wxExecuteEnv environment;
    environment.cwd = workingDirectory;
    pid_ = wxExecute(argv.data(), wxEXEC_ASYNC, process_, &environment);
    if (pid_ == 0) {
        delete process_;
        process_ = nullptr;
        if (error) *error = wxString::Format(wxS("Could not start Code::Blocks adapter: %s"), executable);
        return false;
    }

    inputBuffer_.clear();
    ready_ = false;
    handshakeReceived_ = false;
    contractMajor_ = 0;
    contractMinor_ = 0;
    sdkMajor_ = 0;
    sdkMinor_ = 0;
    sdkRelease_ = 0;
    capabilities_.Clear();
    if (!StartHandshake(configuration)) {
        if (error) *error = wxS("Could not send Code::Blocks adapter handshake.");
        Stop();
        return false;
    }
    return true;
}

void CodeBlocksAdapterClient::Stop()
{
    if (!process_) return;
    if (IsRunning()) {
        SendRaw(wxS("{\"type\":\"shutdown\"}"));
        wxKill(pid_, wxSIGTERM, nullptr, wxKILL_CHILDREN);
    }
    delete process_;
    process_ = nullptr;
    pid_ = 0;
    inputBuffer_.clear();
    ready_ = false;
    handshakeReceived_ = false;
}

bool CodeBlocksAdapterClient::IsRunning() const
{
    return process_ != nullptr && pid_ != 0;
}

bool CodeBlocksAdapterClient::SendRaw(const wxString& jsonLine)
{
    if (!IsRunning() || !process_->GetOutputStream()) return false;
    const wxString line = jsonLine + wxS("\n");
    const wxScopedCharBuffer utf8 = line.utf8_str();
    wxOutputStream* output = process_->GetOutputStream();
    output->Write(utf8.data(), utf8.length());
    output->Sync();
    return output->LastWrite() == utf8.length() && output->IsOk();
}

bool CodeBlocksAdapterClient::StartHandshake(const CodeBlocksHostConfiguration& configuration)
{
    return SendRaw(wxString::Format(
        wxS("{\"type\":\"handshake\",\"contractMajor\":%d,\"contractMinor\":%d,\"sdkMajor\":%d,\"sdkMinor\":%d,\"sdkRelease\":%d,\"sdkRoot\":\"%s\",\"projectFile\":\"%s\"}"),
        configuration.contractMajor, configuration.contractMinor,
        configuration.sdkMajor, configuration.sdkMinor, configuration.sdkRelease,
        JsonEscape(configuration.sdkRoot), JsonEscape(configuration.projectFile)));
}

bool CodeBlocksAdapterClient::OpenProject(const wxString& projectFile)
{
    if (!ready_) return false;
    return SendRaw(wxString::Format(wxS("{\"type\":\"openProject\",\"projectFile\":\"%s\"}"), JsonEscape(projectFile)));
}

bool CodeBlocksAdapterClient::BuildTarget(const wxString& projectFile,
                                          const wxString& target,
                                          const wxString& configuration)
{
    if (!ready_) return false;
    return SendRaw(wxString::Format(
        wxS("{\"type\":\"build\",\"projectFile\":\"%s\",\"target\":\"%s\",\"configuration\":\"%s\"}"),
        JsonEscape(projectFile), JsonEscape(target), JsonEscape(configuration)));
}

wxArrayString CodeBlocksAdapterClient::Poll()
{
    wxArrayString lines;
    if (!IsRunning() || !process_->GetInputStream()) return lines;
    wxInputStream* stream = process_->GetInputStream();
    char byte = 0;
    while (stream->CanRead()) {
        stream->Read(&byte, 1);
        if (stream->LastRead() != 1) break;
        if (byte == '\n') {
            lines.Add(wxString::FromUTF8(inputBuffer_.data(), inputBuffer_.size()));
            inputBuffer_.clear();
        } else if (byte != '\r') {
            inputBuffer_.push_back(byte);
        }
    }
    if (pid_ != 0 && !wxProcess::Exists(pid_)) {
        pid_ = 0;
        delete process_;
        process_ = nullptr;
        ready_ = false;
    }
    return lines;
}

std::vector<CodeBlocksHostEvent> CodeBlocksAdapterClient::PollEvents()
{
    std::vector<CodeBlocksHostEvent> events;
    for (const auto& line : Poll()) ProcessProtocolLine(line, &events);
    return events;
}

void CodeBlocksAdapterClient::ProcessProtocolLine(const wxString& line,
                                                  std::vector<CodeBlocksHostEvent>* events)
{
    const wxString type = JsonStringField(line, wxS("type"));
    if (type == wxS("ready")) {
        handshakeReceived_ = true;
        contractMajor_ = JsonIntField(line, wxS("contractMajor"));
        contractMinor_ = JsonIntField(line, wxS("contractMinor"));
        sdkMajor_ = JsonIntField(line, wxS("sdkMajor"));
        sdkMinor_ = JsonIntField(line, wxS("sdkMinor"));
        sdkRelease_ = JsonIntField(line, wxS("sdkRelease"));
        capabilities_ = JsonStringArrayField(line, wxS("capabilities"));
        ready_ = CodeBlocksHostContract::Supports(contractMajor_, contractMinor_);
        return;
    }
    if (type != wxS("event") || !events) return;

    CodeBlocksHostEvent event;
    event.kind = EventKindFromName(JsonStringField(line, wxS("event")));
    event.projectPath = JsonStringField(line, wxS("projectPath"));
    event.target = JsonStringField(line, wxS("target"));
    event.plugin = JsonStringField(line, wxS("plugin"));
    event.command = JsonStringField(line, wxS("command"));
    event.message = JsonStringField(line, wxS("message"));
    event.filePath = JsonStringField(line, wxS("filePath"));
    event.line = JsonIntField(line, wxS("line"));
    event.column = JsonIntField(line, wxS("column"));
    event.exitCode = JsonIntField(line, wxS("exitCode"));
    event.isError = JsonTrueField(line, wxS("isError"));
    event.payload = line;
    events->push_back(event);
}

} // namespace codium
