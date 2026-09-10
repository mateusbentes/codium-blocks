#include "codium/codeblocks_sdk_bootstrap.hpp"

#include <wx/app.h>
#include <wx/frame.h>
#include <wx/string.h>

#include <cbplugin.h>
#include <cbproject.h>

#include <atomic>
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
                    wxS("\",\"exitCode\":") + wxString::Format(wxS("%d"), event.exitCode) +
                    wxS(",\"isError\":") + (event.isError ? wxS("true") : wxS("false")) +
                    wxS(",\"payload\":\"") + JsonEscape(event.payload) + wxS("\"}");
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
        } else if (argument == "--help") {
            std::cout << "codium-blocks-codeblocks-adapter --data-dir=DIR --compiler-plugin=FILE\n";
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

class AdapterSession final {
public:
    explicit AdapterSession(const AdapterOptions& options)
        : options_(options), bootstrap_(nullptr)
    {
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
            EmitError(wxS("unsupported"), wxS("The Code::Blocks adapter does not build projects yet; real compilation is planned for Phase C."));
        } else if (!type.empty()) {
            EmitError(wxS("unknownRequest"), wxString::Format(wxS("Unknown request type: %s"), type));
        }
    }

    void Shutdown()
    {
        if (bootstrap_) bootstrap_->Shutdown();
    }

private:
    void HandleHandshake(const std::string& line)
    {
        if (ready_) return;
        const int requestedMajor = JsonIntField(line, "contractMajor", 1);
        const int requestedMinor = JsonIntField(line, "contractMinor", 0);
        if (requestedMajor != 1 || requestedMinor < 0 || requestedMinor > 0) {
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
        if (!bootstrap_->Start(options_.dataDirectory, options_.compilerPlugin, &error)) {
            EmitError(wxS("bootstrapFailed"), error);
            stopRequested.store(true);
            if (wxTheApp) wxTheApp->ExitMainLoop();
            return;
        }

        ready_ = true;
        Emit(wxString::Format(
            wxS("{\"type\":\"ready\",\"contractMajor\":1,\"contractMinor\":0,\"sdkMajor\":%d,\"sdkMinor\":%d,\"sdkRelease\":%d,\"sdkIdentity\":\""),
            PLUGIN_SDK_VERSION_MAJOR, PLUGIN_SDK_VERSION_MINOR, PLUGIN_SDK_VERSION_RELEASE) +
             JsonEscape(bootstrap_->Report().sdkIdentity) +
             wxS("\",\"capabilities\":[\"sdkBootstrap\",\"sdkEventSink\",\"projectEvents\",\"projectTargets\",\"compilerEvents\",\"compilerPluginMatched\"]}"));
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
        for (const auto& event : bootstrap_->DrainEvents()) EmitSdkEvent(event);
        for (const auto& target : bootstrap_->EnumerateTargets(project)) {
            Emit(wxS("{\"type\":\"event\",\"event\":\"projectTarget\",\"projectPath\":\"") +
                 JsonEscape(projectFile) + wxS("\",\"target\":\"") + JsonEscape(target.title) +
                 wxS("\",\"compilerId\":\"") + JsonEscape(target.compilerId) +
                 wxS("\",\"outputPath\":\"") + JsonEscape(target.outputPath) +
                 wxS("\",\"workingDirectory\":\"") + JsonEscape(target.workingDirectory) +
                 wxS("\",\"message\":\"Project target enumerated by Code::Blocks SDK\"}"));
        }
    }

    AdapterOptions options_;
    std::unique_ptr<codium::CodeBlocksSdkBootstrap> bootstrap_;
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
    while (frame->GetEventHandler() != frame) frame->PopEventHandler(true);
    delete frame;
    wxTheApp->OnExit();
    wxEntryCleanup();
    return 0;
}
