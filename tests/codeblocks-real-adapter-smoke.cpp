#include "codium/codeblocks_adapter_client.hpp"

#include <wx/init.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/string.h>

#include <iostream>

namespace {

bool WaitForReady(codium::CodeBlocksAdapterClient& adapter)
{
    // The first Code::Blocks plugin bootstrap can be slow on a fresh hosted
    // runner, especially while wxWidgets, the SDK, the Debugger plugin, and
    // the matched private provider initialize resources and shared libraries.
    // Keep this bounded, but do not turn normal cold-runner variance into a
    // false handshake failure after only a few seconds.
    constexpr int kPollIntervalMs = 10;
    constexpr int kHandshakeTimeoutMs = 60 * 1000;
    for (int elapsed = 0; elapsed < kHandshakeTimeoutMs && adapter.IsRunning();
         elapsed += kPollIntervalMs) {
        wxMilliSleep(kPollIntervalMs);
        adapter.PollEvents();
        if (adapter.IsReady()) return true;
    }
    return false;
}

bool LooksLikeValidSnapshotObject(const wxString& json)
{
    wxString trimmed = json;
    trimmed.Trim(true).Trim(false);
    if (trimmed.empty() || trimmed.GetChar(0) != '{' ||
        trimmed.GetChar(trimmed.length() - 1) != '}') return false;
    // FrameJson must not put a string quote after the numeric/null line value.
    // This catches malformed output while keeping the smoke independent of a
    // third-party JSON library.
    return trimmed.Find(wxS("\",\"valid\"")) == wxNOT_FOUND;
}

} // namespace

int main(int argc, char** argv)
{
    wxInitializer initializer;
    if (!initializer.IsOk() || argc < 3) {
        std::cerr << "codeblocks-real-adapter-smoke: initialization or arguments failed\n";
        return 1;
    }

    const wxString adapterPath = wxString::FromUTF8(argv[1]);
    const wxString projectPath = wxString::FromUTF8(argv[2]);
    const wxString dataDirectory = wxString::FromUTF8(argc > 3 ? argv[3] : "");
    const wxString compilerPlugin = wxString::FromUTF8(argc > 4 ? argv[4] : "");
    const wxString debuggerPlugin = wxString::FromUTF8(argc > 5 ? argv[5] : "");
    const wxString debuggerProvider = wxString::FromUTF8(argc > 6 ? argv[6] : "");
    if (dataDirectory.empty() || compilerPlugin.empty()) {
        std::cerr << "codeblocks-real-adapter-smoke: SDK paths are missing\n";
        return 2;
    }
    const wxString fixtureDirectory = wxFileName(projectPath).GetPath();
    wxFileName::Rmdir(fixtureDirectory + wxS("/bin"), wxPATH_RMDIR_RECURSIVE);
    wxFileName::Rmdir(fixtureDirectory + wxS("/obj"), wxPATH_RMDIR_RECURSIVE);

    codium::CodeBlocksAdapterClient adapter(nullptr);
    wxArrayString arguments;
    arguments.Add(wxString::Format(wxS("--data-dir=%s"), dataDirectory));
    arguments.Add(wxString::Format(wxS("--compiler-plugin=%s"), compilerPlugin));
    if (!debuggerPlugin.empty()) arguments.Add(wxString::Format(wxS("--debugger-plugin=%s"), debuggerPlugin));
    if (!debuggerProvider.empty()) arguments.Add(wxString::Format(wxS("--debugger-provider=%s"), debuggerProvider));
    codium::CodeBlocksHostConfiguration configuration;
    configuration.sdkMajor = 0;
    configuration.sdkMinor = 0;
    configuration.sdkRelease = 0;
    configuration.sdkRoot = dataDirectory;
    configuration.projectFile = projectPath;
    wxString error;
    if (!adapter.Start(adapterPath, arguments, dataDirectory, configuration, &error) ||
        !WaitForReady(adapter) ||
        adapter.ContractMajor() != 1 || adapter.ContractMinor() != 3 ||
        adapter.SdkMajor() <= 0 || adapter.SdkMinor() < 0 || adapter.SdkRelease() < 0 ||
        adapter.Capabilities().Index(wxS("projectTargets")) == wxNOT_FOUND ||
        adapter.Capabilities().Index(wxS("sdkEventSink")) == wxNOT_FOUND ||
        adapter.Capabilities().Index(wxS("compilerEvents")) == wxNOT_FOUND ||
        adapter.Capabilities().Index(wxS("compilerPluginMatched")) == wxNOT_FOUND ||
        (!debuggerPlugin.empty() &&
          (adapter.Capabilities().Index(wxS("debuggerPluginMatched")) == wxNOT_FOUND ||
           adapter.Capabilities().Index(wxS("debuggerEvents")) == wxNOT_FOUND ||
           adapter.Capabilities().Index(wxS("debuggerControl")) == wxNOT_FOUND ||
           adapter.Capabilities().Index(wxS("debuggerSnapshot")) == wxNOT_FOUND ||
           adapter.Capabilities().Index(wxS("debuggerDataUnavailable")) == wxNOT_FOUND ||
           (!debuggerProvider.empty() &&
            (adapter.Capabilities().Index(wxS("debuggerPrivateProvider")) == wxNOT_FOUND ||
             adapter.Capabilities().Index(wxS("debuggerStackFrames")) == wxNOT_FOUND ||
             adapter.Capabilities().Index(wxS("debuggerThreads")) == wxNOT_FOUND ||
             adapter.Capabilities().Index(wxS("debuggerBreakpoints")) == wxNOT_FOUND ||
             adapter.Capabilities().Index(wxS("debuggerWatches")) == wxNOT_FOUND ||
             adapter.Capabilities().Index(wxS("debuggerVariables")) == wxNOT_FOUND))))) {
        std::cerr << "codeblocks-real-adapter-smoke: handshake failed: " << error.ToStdString()
                  << " (running=" << (adapter.IsRunning() ? "true" : "false")
                  << ", handshakeReceived=" << (adapter.HandshakeReceived() ? "true" : "false")
                  << ", ready=" << (adapter.IsReady() ? "true" : "false")
                  << ", errorCode=" << adapter.LastErrorCode().ToStdString()
                  << ", errorMessage=" << adapter.LastErrorMessage().ToStdString() << ")\n";
        return 3;
    }

    if (!adapter.OpenProject(projectPath)) {
        std::cerr << "codeblocks-real-adapter-smoke: openProject request failed\n";
        return 4;
    }

    bool opened = false;
    bool officialOpen = false;
    int targetCount = 0;
    bool debugFound = false;
    bool releaseFound = false;
    for (int index = 0; index < 300 && adapter.IsRunning() && targetCount < 2; ++index) {
        wxMilliSleep(10);
        for (const auto& event : adapter.PollEvents()) {
            if (event.kind == codium::CodeBlocksEventKind::ProjectOpened &&
                event.projectPath == projectPath) {
                opened = true;
                officialOpen = event.message.Contains(wxS("cbEVT_PROJECT_OPEN"));
            }
            if (event.kind == codium::CodeBlocksEventKind::ProjectTarget &&
                event.projectPath == projectPath) {
                ++targetCount;
                if (event.target == wxS("Debug") && event.compilerId == wxS("gcc")) debugFound = true;
                if (event.target == wxS("Release") && event.compilerId == wxS("gcc")) releaseFound = true;
            }
        }
    }

    if (!opened || !officialOpen || targetCount != 2 || !debugFound || !releaseFound) {
        adapter.Stop();
        std::cerr << "codeblocks-real-adapter-smoke: project target enumeration failed\n";
        return 5;
    }

    if (!adapter.BuildTarget(projectPath, wxS("Debug"), wxS("Debug"))) {
        adapter.Stop();
        std::cerr << "codeblocks-real-adapter-smoke: real build request failed\n";
        return 6;
    }
    bool buildStarted = false;
    bool buildFinished = false;
    bool compilerOutput = false;
    int buildExitCode = -1;
    // A first Compiler/Debugger plugin launch can spend tens of seconds in
    // dynamic loading and GDB startup on a cold hosted runner. Keep this
    // bounded, but do not turn runner initialization variance into a false
    // build failure.
    constexpr int kBuildPollIntervalMs = 25;
    constexpr int kBuildTimeoutMs = 120 * 1000;
    for (int elapsed = 0;
         elapsed < kBuildTimeoutMs && adapter.IsRunning() && !buildFinished;
         elapsed += kBuildPollIntervalMs) {
        wxMilliSleep(kBuildPollIntervalMs);
        for (const auto& event : adapter.PollEvents()) {
            if (event.kind == codium::CodeBlocksEventKind::BuildStarted) buildStarted = true;
            if (event.kind == codium::CodeBlocksEventKind::CompilerOutput && !event.message.empty()) {
                compilerOutput = true;
            }
            if (event.kind == codium::CodeBlocksEventKind::BuildFinished) {
                buildFinished = true;
                buildExitCode = event.exitCode;
            }
        }
    }
    const wxFileName outputFile(wxFileName(projectPath).GetPath() + wxS("/bin/debug/fixture"));
    if (!debuggerPlugin.empty()) {
        if (!adapter.DebugProject(projectPath, wxS("Debug"), true)) {
            adapter.Stop();
            std::cerr << "codeblocks-real-adapter-smoke: real debug request failed\n";
            return 8;
        }
        bool debugStarted = false;
        bool debugObserved = false;
        bool debugStopped = false;
        bool stopRequested = false;
        bool snapshotRequested = false;
        bool snapshotSeen = false;
        bool privateDataRequested = false;
        bool privateDataRejected = false;
        bool framesSeen = false;
        bool threadsSeen = false;
        bool breakpointsSeen = false;
        bool watchesSeen = false;
        bool variablesSeen = false;
        bool framesJsonValid = false;
        constexpr int kDebugPollIntervalMs = 25;
        constexpr int kDebugTimeoutMs = 120 * 1000;
        for (int elapsed = 0;
             elapsed < kDebugTimeoutMs && adapter.IsRunning() && !debugStopped;
             elapsed += kDebugPollIntervalMs) {
            wxMilliSleep(kDebugPollIntervalMs);
            for (const auto& event : adapter.PollEvents()) {
                if (event.kind == codium::CodeBlocksEventKind::DebugSessionStarted) {
                    debugStarted = true;
                    debugObserved = true;
                }
                if (event.kind == codium::CodeBlocksEventKind::DebugSessionPaused) debugObserved = true;
                if (event.kind == codium::CodeBlocksEventKind::DebugSessionStopped) {
                    debugObserved = true;
                    debugStopped = true;
                }
                if (event.kind == codium::CodeBlocksEventKind::DebugSnapshot &&
                    event.dataKind == wxS("state") && event.payload.Find(wxS("stopped")) != wxNOT_FOUND) {
                    snapshotSeen = true;
                }
                if (event.kind == codium::CodeBlocksEventKind::DebugSnapshot &&
                    event.dataKind == wxS("frames")) {
                    framesSeen = true;
                    framesJsonValid = LooksLikeValidSnapshotObject(event.snapshotJson);
                }
                if (event.kind == codium::CodeBlocksEventKind::DebugSnapshot &&
                    event.dataKind == wxS("threads")) threadsSeen = true;
                if (event.kind == codium::CodeBlocksEventKind::DebugSnapshot &&
                    event.dataKind == wxS("breakpoints")) breakpointsSeen = true;
                if (event.kind == codium::CodeBlocksEventKind::DebugSnapshot &&
                    event.dataKind == wxS("watches") && event.payload.Find(wxS("fixtureValue")) != wxNOT_FOUND)
                    watchesSeen = true;
                if (event.kind == codium::CodeBlocksEventKind::DebugSnapshot &&
                    event.dataKind == wxS("variables") && event.payload.Find(wxS("fixtureValue")) != wxNOT_FOUND)
                    variablesSeen = true;
            }
            if (debugObserved && !snapshotRequested) {
                if (!adapter.RequestDebugSnapshot(wxS("state"))) {
                    adapter.Stop();
                    std::cerr << "codeblocks-real-adapter-smoke: public debugger snapshot request failed\n";
                    return 10;
                }
                snapshotRequested = true;
            }
            if (snapshotSeen && !privateDataRequested) {
                if (!adapter.RequestDebugSnapshot(debuggerProvider.empty() ? wxS("registers") : wxS("frames"))) {
                    adapter.Stop();
                    std::cerr << "codeblocks-real-adapter-smoke: private data request could not be sent\n";
                    return 11;
                }
                privateDataRequested = true;
            }
            if (debuggerProvider.empty()) {
                if (privateDataRequested && adapter.LastErrorCode() == wxS("debugDataUnavailable"))
                    privateDataRejected = true;
            } else if (framesSeen && !threadsSeen) {
                adapter.RequestDebugSnapshot(wxS("threads"));
            } else if (threadsSeen && !breakpointsSeen) {
                adapter.RequestDebugSnapshot(wxS("breakpoints"));
            } else if (breakpointsSeen && !watchesSeen) {
                adapter.RequestDebugSnapshot(wxS("watches"), wxS("fixtureValue"));
            } else if (watchesSeen && !variablesSeen) {
                adapter.RequestDebugSnapshot(wxS("variables"), wxS("fixtureValue"));
            }
            const bool privateDataComplete = debuggerProvider.empty()
                ? privateDataRejected
                : framesSeen && framesJsonValid && threadsSeen && breakpointsSeen && watchesSeen && variablesSeen;
            if (debugStarted && privateDataComplete && !stopRequested)
                stopRequested = adapter.StopDebug();
            if (!debuggerProvider.empty() && stopRequested) break;
        }
        // PollEvents() intentionally stops reading after the child has
        // exited. The adapter can therefore publish debugDataUnavailable
        // just before the final loop condition observes IsRunning()==false;
        // capture the already parsed client error once more before judging
        // the public-ABI probe.
        if (debuggerProvider.empty() && privateDataRequested &&
            adapter.LastErrorCode() == wxS("debugDataUnavailable")) {
            privateDataRejected = true;
        }
        const bool privateDataComplete = debuggerProvider.empty()
            ? privateDataRejected
            : framesSeen && framesJsonValid && threadsSeen && breakpointsSeen && watchesSeen && variablesSeen;
        // A public-SDK run may finish naturally immediately after the adapter
        // reports debugDataUnavailable for a private model kind. In that
        // case the process has already ended, so there is no later
        // cbEVT_DEBUGGER_FINISHED event left to observe through the pipe.
        // Treat that bounded, explicit termination as equivalent to the
        // normal stop event; provider-backed runs still require our stop
        // request to be accepted.
        const bool sessionEnded = debuggerProvider.empty()
            ? (debugStopped || (!adapter.IsRunning() && privateDataComplete))
            : stopRequested;
        if (!debugStarted || !debugObserved || !snapshotSeen || !privateDataComplete || !sessionEnded) {
            adapter.Stop();
            std::cerr << "codeblocks-real-adapter-smoke: debugger events were not observed"
                      << " (error=" << adapter.LastErrorCode().ToStdString()
                      << ":" << adapter.LastErrorMessage().ToStdString()
                      << ", running=" << (adapter.IsRunning() ? "true" : "false") << ")\n";
            return 14;
        }
    }
    adapter.Stop();
    if (!buildStarted || !compilerOutput || !buildFinished || buildExitCode != 0 ||
        !outputFile.FileExists()) {
        std::cerr << "codeblocks-real-adapter-smoke: compiler build events or output failed\n";
        return 7;
    }

    if (!debuggerPlugin.empty()) {
        codium::CodeBlocksAdapterClient untrustedAdapter(nullptr);
        wxArrayString untrustedArguments;
        untrustedArguments.Add(wxString::Format(wxS("--data-dir=%s"), dataDirectory));
        untrustedArguments.Add(wxString::Format(wxS("--compiler-plugin=%s"), compilerPlugin));
        untrustedArguments.Add(wxString::Format(wxS("--debugger-plugin=%s"), debuggerPlugin));
        if (!debuggerProvider.empty())
            untrustedArguments.Add(wxString::Format(wxS("--debugger-provider=%s"), debuggerProvider));
        untrustedArguments.Add(wxS("--workspace-untrusted"));
        if (!untrustedAdapter.Start(adapterPath, untrustedArguments, dataDirectory, configuration, &error)) {
            std::cerr << "codeblocks-real-adapter-smoke: trust policy probe could not start\n";
            return 15;
        }
        for (int index = 0; index < 300 && untrustedAdapter.IsRunning(); ++index) {
            wxMilliSleep(10);
            untrustedAdapter.PollEvents();
            if (!untrustedAdapter.LastErrorCode().empty()) break;
        }
        if (untrustedAdapter.LastErrorCode() != wxS("bootstrapFailed") || untrustedAdapter.IsReady()) {
            std::cerr << "codeblocks-real-adapter-smoke: untrusted debugger was accepted\n";
            return 16;
        }
        untrustedAdapter.Stop();
    }

    codium::CodeBlocksAdapterClient failedAdapter(nullptr);
    wxArrayString failedArguments;
    failedArguments.Add(wxString::Format(wxS("--data-dir=%s/missing"), dataDirectory));
    failedArguments.Add(wxString::Format(wxS("--compiler-plugin=%s"), compilerPlugin));
    if (!failedAdapter.Start(adapterPath, failedArguments, dataDirectory, configuration, &error)) {
        std::cerr << "codeblocks-real-adapter-smoke: failure probe could not start\n";
            return 17;
    }
    for (int index = 0; index < 300 && failedAdapter.IsRunning(); ++index) {
        wxMilliSleep(10);
        failedAdapter.PollEvents();
        if (!failedAdapter.LastErrorCode().empty()) break;
    }
    if (failedAdapter.LastErrorCode() != wxS("bootstrapFailed") || failedAdapter.IsReady()) {
        std::cerr << "codeblocks-real-adapter-smoke: resource failure was not reported safely\n";
            return 18;
    }
    failedAdapter.Stop();

    std::cout << "codeblocks-real-adapter-smoke: ok — matched SDK bootstrap and real .cbp target enumeration\n";
    return 0;
}
