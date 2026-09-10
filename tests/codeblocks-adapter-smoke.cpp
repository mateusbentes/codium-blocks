#include "codium/codeblocks_adapter_client.hpp"

#include <wx/init.h>
#include <wx/string.h>

#include <iostream>

namespace {

bool WaitForReady(codium::CodeBlocksAdapterClient& adapter)
{
    for (int i = 0; i < 200 && adapter.IsRunning(); ++i) {
        wxMilliSleep(10);
        adapter.PollEvents();
        if (adapter.IsReady()) return true;
    }
    return false;
}

bool WaitForBuildEvents(codium::CodeBlocksAdapterClient& adapter,
                        codium::CodeBlocksHostEvent* diagnostic)
{
    bool started = false;
    bool diagnosed = false;
    bool finished = false;
    for (int i = 0; i < 200 && adapter.IsRunning() && !finished; ++i) {
        wxMilliSleep(10);
        for (const auto& event : adapter.PollEvents()) {
            if (event.kind == codium::CodeBlocksEventKind::BuildStarted) started = true;
            if (event.kind == codium::CodeBlocksEventKind::CompilerDiagnostic) {
                diagnosed = true;
                if (diagnostic) *diagnostic = event;
            }
            if (event.kind == codium::CodeBlocksEventKind::BuildFinished) finished = true;
        }
    }
    return started && diagnosed && finished;
}

} // namespace

int main(int argc, char** argv)
{
    wxInitializer initializer;
    if (!initializer.IsOk() || argc < 2) {
        std::cerr << "codeblocks-adapter-smoke: initialization or test root failed\n";
        return 1;
    }

    const wxString root = wxString::FromUTF8(argv[1]);
    const wxString fakeAdapter = root + wxS("/tests/fake-codeblocks-adapter.mjs");
    codium::CodeBlocksAdapterClient adapter(nullptr);
    wxArrayString arguments;
    arguments.Add(fakeAdapter);
    codium::CodeBlocksHostConfiguration configuration;
    configuration.sdkMajor = 1;
    configuration.sdkMinor = 36;
    configuration.sdkRelease = 0;
    configuration.sdkRoot = root;
    configuration.projectFile = root + wxS("/demo.cbp");
    wxString error;

    if (!adapter.Start(wxS("node"), arguments, root, configuration, &error) ||
        !WaitForReady(adapter) || adapter.ContractMajor() != 1 || adapter.ContractMinor() != 0 ||
        adapter.SdkMajor() != 1 || adapter.SdkMinor() != 36 ||
        adapter.Capabilities().Index(wxS("sdkEventSink")) == wxNOT_FOUND ||
        adapter.Capabilities().Index(wxS("compilerEvents")) == wxNOT_FOUND ||
        adapter.Capabilities().Index(wxS("compilerDiagnostics")) == wxNOT_FOUND ||
        adapter.Capabilities().Index(wxS("debuggerEvents")) == wxNOT_FOUND ||
        adapter.Capabilities().Index(wxS("debuggerControl")) == wxNOT_FOUND) {
        std::cerr << "codeblocks-adapter-smoke: handshake failed: " << error.ToStdString() << "\n";
        return 2;
    }

    if (!adapter.OpenProject(configuration.projectFile)) {
        std::cerr << "codeblocks-adapter-smoke: openProject request failed\n";
        return 3;
    }
    codium::CodeBlocksHostEvent opened;
    codium::CodeBlocksHostEvent target;
    bool openedSeen = false;
    bool targetSeen = false;
    for (int i = 0; i < 200 && adapter.IsRunning() && (!openedSeen || !targetSeen); ++i) {
        wxMilliSleep(10);
        for (const auto& event : adapter.PollEvents()) {
            if (event.kind == codium::CodeBlocksEventKind::ProjectOpened) {
                opened = event;
                openedSeen = true;
            }
            if (event.kind == codium::CodeBlocksEventKind::ProjectTarget) {
                target = event;
                targetSeen = true;
            }
        }
    }
    if (!openedSeen || opened.projectPath != configuration.projectFile ||
        !targetSeen || target.target != wxS("app") || target.compilerId != wxS("gcc") ||
        target.outputPath != wxS("bin/app") || target.workingDirectory != wxS("bin")) {
        std::cerr << "codeblocks-adapter-smoke: project target events failed\n";
        return 4;
    }

    if (!adapter.BuildTarget(configuration.projectFile, wxS("app"), wxS("Debug"))) {
        std::cerr << "codeblocks-adapter-smoke: build request failed\n";
        return 5;
    }
    codium::CodeBlocksHostEvent diagnostic;
    if (!WaitForBuildEvents(adapter, &diagnostic) ||
        diagnostic.filePath != wxS("src/main.cpp") || diagnostic.line != 12 || diagnostic.column != 4 ||
        diagnostic.isError || diagnostic.message != wxS("fake warning")) {
        std::cerr << "codeblocks-adapter-smoke: build event flow failed\n";
        return 6;
    }

    if (!adapter.DebugProject(configuration.projectFile, wxS("app"), true)) {
        std::cerr << "codeblocks-adapter-smoke: debug request failed\n";
        return 7;
    }
    bool debugStarted = false;
    bool debugPaused = false;
    for (int i = 0; i < 200 && adapter.IsRunning() && !debugPaused; ++i) {
        wxMilliSleep(10);
        for (const auto& event : adapter.PollEvents()) {
            if (event.kind == codium::CodeBlocksEventKind::DebugSessionStarted) debugStarted = true;
            if (event.kind == codium::CodeBlocksEventKind::DebugSessionPaused) debugPaused = true;
        }
    }
    if (!debugStarted || !debugPaused || !adapter.StopDebug()) {
        std::cerr << "codeblocks-adapter-smoke: debug event flow failed\n";
        return 8;
    }

    adapter.Stop();
    codium::CodeBlocksAdapterClient incompatibleAdapter(nullptr);
    wxArrayString incompatibleArguments;
    incompatibleArguments.Add(fakeAdapter);
    incompatibleArguments.Add(wxS("--incompatible"));
    if (!incompatibleAdapter.Start(wxS("node"), incompatibleArguments, root, configuration, &error)) {
        std::cerr << "codeblocks-adapter-smoke: incompatible adapter could not start\n";
        return 9;
    }
    for (int i = 0; i < 200 && incompatibleAdapter.IsRunning() && !incompatibleAdapter.HandshakeReceived(); ++i) {
        wxMilliSleep(10);
        incompatibleAdapter.PollEvents();
    }
    if (!incompatibleAdapter.HandshakeReceived() || incompatibleAdapter.IsReady()) {
        std::cerr << "codeblocks-adapter-smoke: incompatible contract was accepted\n";
        return 10;
    }
    incompatibleAdapter.Stop();
    std::cout << "codeblocks-adapter-smoke: ok — JSON Lines handshake, capabilities, project, build, and diagnostics\n";
    return 0;
}
