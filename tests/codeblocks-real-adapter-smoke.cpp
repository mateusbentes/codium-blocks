#include "codium/codeblocks_adapter_client.hpp"

#include <wx/init.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/string.h>

#include <iostream>

namespace {

bool WaitForReady(codium::CodeBlocksAdapterClient& adapter)
{
    for (int index = 0; index < 300 && adapter.IsRunning(); ++index) {
        wxMilliSleep(10);
        adapter.PollEvents();
        if (adapter.IsReady()) return true;
    }
    return false;
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
    codium::CodeBlocksHostConfiguration configuration;
    configuration.sdkMajor = 0;
    configuration.sdkMinor = 0;
    configuration.sdkRelease = 0;
    configuration.sdkRoot = dataDirectory;
    configuration.projectFile = projectPath;
    wxString error;
    if (!adapter.Start(adapterPath, arguments, dataDirectory, configuration, &error) ||
        !WaitForReady(adapter) ||
        adapter.ContractMajor() != 1 || adapter.ContractMinor() != 0 ||
        adapter.SdkMajor() <= 0 || adapter.SdkMinor() < 0 || adapter.SdkRelease() < 0 ||
        adapter.Capabilities().Index(wxS("projectTargets")) == wxNOT_FOUND ||
        adapter.Capabilities().Index(wxS("sdkEventSink")) == wxNOT_FOUND ||
        adapter.Capabilities().Index(wxS("compilerEvents")) == wxNOT_FOUND ||
        adapter.Capabilities().Index(wxS("compilerPluginMatched")) == wxNOT_FOUND) {
        std::cerr << "codeblocks-real-adapter-smoke: handshake failed: " << error.ToStdString() << "\n";
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
    for (int index = 0; index < 1200 && adapter.IsRunning() && !buildFinished; ++index) {
        wxMilliSleep(25);
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
    adapter.Stop();
    if (!buildStarted || !compilerOutput || !buildFinished || buildExitCode != 0 ||
        !outputFile.FileExists()) {
        std::cerr << "codeblocks-real-adapter-smoke: compiler build events or output failed\n";
        return 7;
    }

    codium::CodeBlocksAdapterClient failedAdapter(nullptr);
    wxArrayString failedArguments;
    failedArguments.Add(wxString::Format(wxS("--data-dir=%s/missing"), dataDirectory));
    failedArguments.Add(wxString::Format(wxS("--compiler-plugin=%s"), compilerPlugin));
    if (!failedAdapter.Start(adapterPath, failedArguments, dataDirectory, configuration, &error)) {
        std::cerr << "codeblocks-real-adapter-smoke: failure probe could not start\n";
        return 8;
    }
    for (int index = 0; index < 300 && failedAdapter.IsRunning(); ++index) {
        wxMilliSleep(10);
        failedAdapter.PollEvents();
        if (!failedAdapter.LastErrorCode().empty()) break;
    }
    if (failedAdapter.LastErrorCode() != wxS("bootstrapFailed") || failedAdapter.IsReady()) {
        std::cerr << "codeblocks-real-adapter-smoke: resource failure was not reported safely\n";
        return 9;
    }
    failedAdapter.Stop();

    std::cout << "codeblocks-real-adapter-smoke: ok — matched SDK bootstrap and real .cbp target enumeration\n";
    return 0;
}
