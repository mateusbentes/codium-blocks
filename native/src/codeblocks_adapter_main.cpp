#include "codium/codeblocks_sdk_bootstrap.hpp"

#include <wx/app.h>
#include <wx/frame.h>
#include <wx/string.h>
#include <wx/timer.h>

#include <cbplugin.h>
#include <cbproject.h>

#include <atomic>
#include <algorithm>
#include <memory>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

class AdapterApp final : public wxApp {
public:
    bool OnInit() override { return true; }
};

wxIMPLEMENT_APP_NO_MAIN(AdapterApp);

struct AdapterOptions final {
    wxString dataDirectory;
    wxString compilerPlugin;
    wxString debuggerPlugin;
    wxString debuggerProvider;
    bool workspaceTrusted = true;
};

std::mutex outputMutex;
std::atomic<bool> stopRequested{false};
int protocolFd = -1;

wxString JsonEscape(const wxString& value)
{
    wxString escaped;
    escaped.reserve(value.length() + 8);
    for (const wxChar ch : value) {
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

wxString JsonStringField(const std::string& line, const char* field)
{
    const std::string marker = std::string("\"") + field + "\":\"";
    const size_t start = line.find(marker);
    if (start == std::string::npos) return wxEmptyString;
    const size_t valueStart = start + marker.size();
    size_t valueEnd = valueStart;
    bool escaped = false;
    while (valueEnd < line.size()) {
        const char ch = line[valueEnd];
        if (ch == '"' && !escaped) break;
        escaped = ch == '\\' && !escaped;
        if (ch != '\\') escaped = false;
        ++valueEnd;
    }
    const std::string raw = line.substr(valueStart, valueEnd - valueStart);
    std::string decoded;
    decoded.reserve(raw.size());
    for (size_t index = 0; index < raw.size(); ++index) {
        if (raw[index] == '\\' && index + 1 < raw.size()) {
            const char escapedCharacter = raw[++index];
            if (escapedCharacter == 'n') decoded.push_back('\n');
            else if (escapedCharacter == 'r') decoded.push_back('\r');
            else if (escapedCharacter == 't') decoded.push_back('\t');
            else decoded.push_back(escapedCharacter);
        } else {
            decoded.push_back(raw[index]);
        }
    }
    return wxString::FromUTF8(decoded.data(), decoded.size());
}

int JsonIntField(const std::string& line, const char* field, int fallback = 0)
{
    const std::string marker = std::string("\"") + field + "\":";
    const size_t start = line.find(marker);
    if (start == std::string::npos) return fallback;
    size_t index = start + marker.size();
    while (index < line.size() && (line[index] == ' ' || line[index] == '\t')) ++index;
    int sign = 1;
    if (index < line.size() && line[index] == '-') {
        sign = -1;
        ++index;
    }
    int value = 0;
    bool found = false;
    while (index < line.size() && line[index] >= '0' && line[index] <= '9') {
        value = value * 10 + (line[index] - '0');
        found = true;
        ++index;
    }
    return found ? sign * value : fallback;
}

bool JsonBoolField(const std::string& line, const char* field, bool fallback = false)
{
    const std::string marker = std::string("\"") + field + "\":";
    const size_t start = line.find(marker);
    if (start == std::string::npos) return fallback;
    size_t index = start + marker.size();
    while (index < line.size() && (line[index] == ' ' || line[index] == '\t')) ++index;
    if (line.compare(index, 4, "true") == 0) return true;
    if (line.compare(index, 5, "false") == 0) return false;
    return JsonIntField(line, field, fallback ? 1 : 0) != 0;
}

void Emit(const wxString& json)
{
    const wxString line = json + wxS("\n");
    const wxScopedCharBuffer utf8 = line.utf8_str();
    std::lock_guard<std::mutex> lock(outputMutex);
    if (protocolFd < 0) return;
#ifdef _WIN32
    _write(protocolFd, utf8.data(), static_cast<unsigned int>(utf8.length()));
#else
    ::write(protocolFd, utf8.data(), utf8.length());
#endif
}

void EmitError(const wxString& code, const wxString& message)
{
    Emit(wxS("{\"type\":\"error\",\"code\":\"") + JsonEscape(code) +
         wxS("\",\"message\":\"") + JsonEscape(message) + wxS("\"}"));
}

void EmitDataUnavailable(const wxString& dataKind, const wxString& message)
{
    Emit(wxS("{\"type\":\"error\",\"code\":\"debugDataUnavailable\",\"dataKind\":\"") +
         JsonEscape(dataKind) + wxS("\",\"capability\":\"debuggerDataUnavailable\",\"message\":\"") +
         JsonEscape(message) + wxS("\"}"));
}

void EmitSdkEvent(const codium::CodeBlocksHostEvent& event)
{
    wxString json = wxS("{\"type\":\"event\",\"event\":\"") +
                    JsonEscape(codium::CodeBlocksEventKindName(event.kind)) +
                    wxS("\",\"projectPath\":\"") + JsonEscape(event.projectPath) +
                    wxS("\",\"target\":\"") + JsonEscape(event.target) +
                    wxS("\",\"plugin\":\"") + JsonEscape(event.plugin) +
                    wxS("\",\"message\":\"") + JsonEscape(event.message) +
                    wxS("\",\"filePath\":\"") + JsonEscape(event.filePath) +
                    wxS("\",\"oldFilePath\":\"") + JsonEscape(event.oldFilePath) +
                    wxS("\",\"compilerId\":\"") + JsonEscape(event.compilerId) +
                    wxS("\",\"outputPath\":\"") + JsonEscape(event.outputPath) +
                    wxS("\",\"workingDirectory\":\"") + JsonEscape(event.workingDirectory) +
                    wxS("\",\"line\":") + wxString::Format(wxS("%d"), event.line) +
                    wxS(",\"column\":") + wxString::Format(wxS("%d"), event.column) +
                    wxS(",\"exitCode\":") + wxString::Format(wxS("%d"), event.exitCode) +
                    wxS(",\"isError\":") + (event.isError ? wxS("true") : wxS("false")) +
                    wxS(",\"payload\":\"") + JsonEscape(event.payload) +
                    wxS("\",\"dataKind\":\"") + JsonEscape(event.dataKind) +
                    wxS("\",\"snapshotJson\":\"") + JsonEscape(event.snapshotJson) + wxS("\"}");
    Emit(json);
}

bool RedirectSdkStdout()
{
#ifdef _WIN32
    protocolFd = _dup(_fileno(stdout));
    return protocolFd >= 0 && _dup2(_fileno(stderr), _fileno(stdout)) == 0;
#else
    protocolFd = ::dup(STDOUT_FILENO);
    return protocolFd >= 0 && ::dup2(STDERR_FILENO, STDOUT_FILENO) >= 0;
#endif
}

bool ParseOptions(int argc, char** argv, AdapterOptions* options, wxString* error)
{
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] ? argv[index] : "";
        const auto valueAfter = [&](const char* name) -> wxString {
            const std::string prefix = std::string(name) + "=";
            if (argument.rfind(prefix, 0) != 0) return wxEmptyString;
            return wxString::FromUTF8(argument.substr(prefix.size()));
        };
        if (argument.rfind("--data-dir=", 0) == 0) {
            options->dataDirectory = valueAfter("--data-dir");
        } else if (argument.rfind("--compiler-plugin=", 0) == 0) {
            options->compilerPlugin = valueAfter("--compiler-plugin");
        } else if (argument.rfind("--debugger-plugin=", 0) == 0) {
            options->debuggerPlugin = valueAfter("--debugger-plugin");
        } else if (argument.rfind("--debugger-provider=", 0) == 0) {
            options->debuggerProvider = valueAfter("--debugger-provider");
        } else if (argument == "--workspace-untrusted") {
            options->workspaceTrusted = false;
        } else if (argument == "--help") {
            std::cout << "codium-blocks-codeblocks-adapter --data-dir=DIR --compiler-plugin=FILE [--debugger-plugin=FILE] [--debugger-provider=FILE] [--workspace-untrusted]\n";
            return false;
        } else {
            if (error) *error = wxString::Format(wxS("Unknown adapter option: %s"),
                                                 wxString::FromUTF8(argument));
            return false;
        }
    }
    if (options->dataDirectory.empty() || options->compilerPlugin.empty()) {
        if (error) *error = wxS("Both --data-dir and --compiler-plugin are required.");
        return false;
    }
    return true;
}

class AdapterSession final : public wxEvtHandler {
public:
    explicit AdapterSession(const AdapterOptions& options)
        : options_(options), bootstrap_(nullptr), eventTimer_(this)
    {
        Bind(wxEVT_TIMER, &AdapterSession::OnTimer, this);
    }

    ~AdapterSession() override
    {
        eventTimer_.Stop();
        Unbind(wxEVT_TIMER, &AdapterSession::OnTimer, this);
    }

    void SetFrame(wxFrame* frame)
    {
        bootstrap_ = std::make_unique<codium::CodeBlocksSdkBootstrap>(frame);
    }

    void Handle(const std::string& line)
    {
        const wxString type = JsonStringField(line, "type");
        if (type == wxS("handshake")) {
            HandleHandshake(line);
        } else if (type == wxS("openProject")) {
            HandleOpenProject(JsonStringField(line, "projectFile"));
        } else if (type == wxS("shutdown")) {
            stopRequested.store(true);
            if (wxTheApp) wxTheApp->ExitMainLoop();
        } else if (type == wxS("build")) {
            HandleBuild(JsonStringField(line, "projectFile"),
                        JsonStringField(line, "target"),
                        JsonStringField(line, "configuration"));
        } else if (type == wxS("debug")) {
            HandleDebug(JsonStringField(line, "projectFile"),
                        JsonStringField(line, "target"),
                        JsonBoolField(line, "breakOnEntry", false));
        } else if (type == wxS("continueDebug")) {
            HandleDebugControl(wxS("continue"));
        } else if (type == wxS("pauseDebug")) {
            HandleDebugControl(wxS("pause"));
        } else if (type == wxS("stopDebug")) {
            HandleDebugControl(wxS("stop"));
        } else if (type == wxS("requestDebugSnapshot")) {
            HandleDebugSnapshot(JsonStringField(line, "dataKind"),
                                JsonStringField(line, "expression"));
        } else if (!type.empty()) {
            EmitError(wxS("unknownRequest"), wxString::Format(wxS("Unknown request type: %s"), type));
        }
    }

    void Shutdown()
    {
        eventTimer_.Stop();
        if (bootstrap_) bootstrap_->Shutdown();
    }

private:
    void OnTimer(wxTimerEvent&)
    {
        if (bootstrap_) EmitPendingSdkEvents();
    }

    void HandleHandshake(const std::string& line)
    {
        if (ready_) return;
        const int requestedMajor = JsonIntField(line, "contractMajor", 1);
        const int requestedMinor = JsonIntField(line, "contractMinor", 0);
        if (requestedMajor != 1 || requestedMinor < 0 || requestedMinor > codium::CodeBlocksHostContract::kMinor) {
            Emit(wxString::Format(
                wxS("{\"type\":\"ready\",\"contractMajor\":2,\"contractMinor\":0,\"sdkMajor\":%d,\"sdkMinor\":%d,\"sdkRelease\":%d,\"capabilities\":[]}"),
                PLUGIN_SDK_VERSION_MAJOR, PLUGIN_SDK_VERSION_MINOR, PLUGIN_SDK_VERSION_RELEASE));
            stopRequested.store(true);
            if (wxTheApp) wxTheApp->ExitMainLoop();
            return;
        }

        const int requestedSdkMajor = JsonIntField(line, "sdkMajor", 0);
        const int requestedSdkMinor = JsonIntField(line, "sdkMinor", 0);
        const int requestedSdkRelease = JsonIntField(line, "sdkRelease", 0);
        if (requestedSdkMajor != 0 &&
            (requestedSdkMajor != PLUGIN_SDK_VERSION_MAJOR ||
             requestedSdkMinor != PLUGIN_SDK_VERSION_MINOR ||
             requestedSdkRelease != PLUGIN_SDK_VERSION_RELEASE)) {
            EmitError(wxS("sdkMismatch"), wxString::Format(
                wxS("Requested Code::Blocks SDK %d.%d.%d does not match adapter SDK %d.%d.%d."),
                requestedSdkMajor, requestedSdkMinor, requestedSdkRelease,
                PLUGIN_SDK_VERSION_MAJOR, PLUGIN_SDK_VERSION_MINOR, PLUGIN_SDK_VERSION_RELEASE));
            stopRequested.store(true);
            if (wxTheApp) wxTheApp->ExitMainLoop();
            return;
        }

        wxString error;
        if (!bootstrap_->Start(options_.dataDirectory, options_.compilerPlugin, &error,
                               options_.debuggerPlugin, options_.debuggerProvider,
                               wxEmptyString, options_.workspaceTrusted)) {
            EmitError(wxS("bootstrapFailed"), error);
            stopRequested.store(true);
            if (wxTheApp) wxTheApp->ExitMainLoop();
            return;
        }

        ready_ = true;
        wxString capabilities = wxS("\"sdkBootstrap\",\"sdkEventSink\",\"projectEvents\",\"projectTargets\",\"compilerEvents\",\"compilerBuild\",\"compilerOutput\",\"compilerPluginMatched\"");
        if (bootstrap_->Report().debuggerPluginAttached) {
            capabilities += wxS(",\"debuggerPluginMatched\",\"debuggerEvents\",\"debuggerControl\",\"debuggerSnapshot\",\"debuggerPublicState\",\"debuggerDataUnavailable\"");
        }
        if (bootstrap_->Report().debuggerPrivateProviderAttached) {
            capabilities += wxS(",\"debuggerPrivateProvider\",\"debuggerStackFrames\",\"debuggerThreads\",\"debuggerBreakpoints\",\"debuggerWatches\",\"debuggerVariables\"");
        }
        const int negotiatedMinor = std::min(requestedMinor, codium::CodeBlocksHostContract::kMinor);
        Emit(wxString::Format(
            wxS("{\"type\":\"ready\",\"contractMajor\":1,\"contractMinor\":%d,\"sdkMajor\":%d,\"sdkMinor\":%d,\"sdkRelease\":%d,\"sdkIdentity\":\""),
            negotiatedMinor,
            PLUGIN_SDK_VERSION_MAJOR, PLUGIN_SDK_VERSION_MINOR, PLUGIN_SDK_VERSION_RELEASE) +
             JsonEscape(bootstrap_->Report().sdkIdentity) +
             wxS("\",\"debuggerProviderIdentity\":\"") +
             JsonEscape(bootstrap_->Report().debuggerProviderIdentity) +
             wxS("\",\"debuggerProviderSourceRevision\":\"") +
             JsonEscape(bootstrap_->Report().debuggerProviderSourceRevision) +
             wxS("\",\"debuggerProviderAbiIdentity\":\"") +
             JsonEscape(bootstrap_->Report().debuggerProviderAbiIdentity) +
             wxS("\",\"capabilities\":[") + capabilities + wxS("]}"));
    }

    void HandleOpenProject(const wxString& projectFile)
    {
        if (!ready_) {
            EmitError(wxS("notReady"), wxS("The Code::Blocks SDK adapter is not ready."));
            return;
        }
        wxString error;
        cbProject* project = bootstrap_->LoadProject(projectFile, &error);
        if (!project) {
            EmitError(wxS("projectLoadFailed"), error);
            return;
        }
        EmitPendingSdkEvents();
        for (const auto& target : bootstrap_->EnumerateTargets(project)) {
            Emit(wxS("{\"type\":\"event\",\"event\":\"projectTarget\",\"projectPath\":\"") +
                 JsonEscape(projectFile) + wxS("\",\"target\":\"") + JsonEscape(target.title) +
                 wxS("\",\"compilerId\":\"") + JsonEscape(target.compilerId) +
                 wxS("\",\"outputPath\":\"") + JsonEscape(target.outputPath) +
                 wxS("\",\"workingDirectory\":\"") + JsonEscape(target.workingDirectory) +
                 wxS("\",\"message\":\"Project target enumerated by Code::Blocks SDK\"}"));
        }
    }

    void HandleBuild(const wxString& projectFile,
                     const wxString& target,
                     const wxString& configuration)
    {
        if (!ready_) {
            EmitError(wxS("notReady"), wxS("The Code::Blocks SDK adapter is not ready."));
            return;
        }
        wxString error;
        if (!bootstrap_->BuildProject(projectFile, target, configuration, &error)) {
            EmitError(wxS("buildStartFailed"), error);
            return;
        }
        EmitPendingSdkEvents();
        eventTimer_.Start(25);
    }

    void HandleDebug(const wxString& projectFile, const wxString& target, bool breakOnEntry)
    {
        if (!ready_) {
            EmitError(wxS("notReady"), wxS("The Code::Blocks SDK adapter is not ready."));
            return;
        }
        wxString error;
        if (!bootstrap_->StartDebug(projectFile, target, breakOnEntry, &error)) {
            EmitError(wxS("debugStartFailed"), error);
            return;
        }
        EmitPendingSdkEvents();
        eventTimer_.Start(25);
    }

    void HandleDebugControl(const wxString& action)
    {
        if (!ready_) {
            EmitError(wxS("notReady"), wxS("The Code::Blocks SDK adapter is not ready."));
            return;
        }
        wxString error;
        bool accepted = false;
        if (action == wxS("continue")) accepted = bootstrap_->ContinueDebug(&error);
        else if (action == wxS("pause")) accepted = bootstrap_->PauseDebug(&error);
        else if (action == wxS("stop")) accepted = bootstrap_->StopDebug(&error);
        if (!accepted) {
            EmitError(wxS("debugControlFailed"), error);
            return;
        }
        EmitPendingSdkEvents();
        eventTimer_.Start(25);
    }

    void HandleDebugSnapshot(const wxString& dataKind, const wxString& expression)
    {
        if (!ready_) {
            EmitError(wxS("notReady"), wxS("The Code::Blocks SDK adapter is not ready."));
            return;
        }
        const wxString requested = dataKind.empty() ? wxS("state") : dataKind;
        wxString error;
        if (!bootstrap_->RequestDebugSnapshot(requested, expression, &error)) {
            if (requested != wxS("state")) EmitDataUnavailable(requested, error);
            else EmitError(wxS("debugSnapshotFailed"), error);
            return;
        }
        EmitPendingSdkEvents();
    }

    void EmitPendingSdkEvents()
    {
        for (const auto& event : bootstrap_->DrainEvents()) EmitSdkEvent(event);
    }

    AdapterOptions options_;
    std::unique_ptr<codium::CodeBlocksSdkBootstrap> bootstrap_;
    wxTimer eventTimer_;
    bool ready_ = false;
};

} // namespace

int main(int argc, char** argv)
{
    if (!RedirectSdkStdout()) return 3;
    AdapterOptions options;
    wxString error;
    if (!ParseOptions(argc, argv, &options, &error)) {
        if (!error.empty()) EmitError(wxS("invalidArguments"), error);
        return error.empty() ? 0 : 2;
    }
    // ConfigManager discovers the Code::Blocks data root during its first
    // Manager initialization. Set the official override before wx starts so
    // manager_resources.zip and resources.zip resolve deterministically.
    wxSetEnv(wxS("CODEBLOCKS_DATA_DIR"), options.dataDirectory);
    if (!wxEntryStart(argc, argv)) {
        EmitError(wxS("wxStartupFailed"), wxS("wxEntryStart failed."));
        return 3;
    }
    wxTheApp->CallOnInit();
    wxTheApp->SetExitOnFrameDelete(false);
    auto* frame = new wxFrame(nullptr, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(1, 1));
    frame->Hide();

    AdapterSession session(options);
    session.SetFrame(frame);
    std::thread inputThread([&session] {
        std::string line;
        while (!stopRequested.load() && std::getline(std::cin, line)) {
            if (wxTheApp) wxTheApp->CallAfter([&session, line] { session.Handle(line); });
        }
        if (!stopRequested.exchange(true) && wxTheApp) {
            if (wxTheApp) wxTheApp->CallAfter([] { if (wxTheApp) wxTheApp->ExitMainLoop(); });
        }
    });

    wxTheApp->MainLoop();
    stopRequested.store(true);
    if (inputThread.joinable()) inputThread.join();
    session.Shutdown();
    while (frame->GetEventHandler() != frame) {
        wxEvtHandler* handler = frame->GetEventHandler();
        if (!handler || !handler->GetNextHandler()) {
            // Some Code::Blocks debugger builds leave a terminal handler after
            // plugin teardown. Do not call PopEventHandler on that malformed
            // stack; the adapter is already leaving its dedicated process.
            frame->SetEventHandler(frame);
            break;
        }
        frame->PopEventHandler(true);
    }
    delete frame;
    wxTheApp->OnExit();
    wxEntryCleanup();
    return 0;
}
