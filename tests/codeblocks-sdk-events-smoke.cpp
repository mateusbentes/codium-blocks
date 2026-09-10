#include "codium/codeblocks_sdk_bootstrap.hpp"

#include <wx/app.h>
#include <wx/frame.h>

#include <cbplugin.h>
#include <cbproject.h>
#include <manager.h>
#include <pluginmanager.h>
#include <sdk_events.h>

#include <iostream>

namespace {

class TestApp final : public wxApp {
public:
    bool OnInit() override { return true; }
};

wxIMPLEMENT_APP_NO_MAIN(TestApp);

bool HasEvent(const std::vector<codium::CodeBlocksHostEvent>& events,
              codium::CodeBlocksEventKind kind,
              const wxString& messagePart = wxEmptyString)
{
    for (const auto& event : events) {
        if (event.kind == kind &&
            (messagePart.empty() || event.message.Contains(messagePart))) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4 || !wxEntryStart(argc, argv)) {
        std::cerr << "codeblocks-sdk-events-smoke: initialization or arguments failed\n";
        return 1;
    }
    wxTheApp->CallOnInit();
    wxTheApp->SetExitOnFrameDelete(false);
    auto* frame = new wxFrame(nullptr, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(1, 1));
    frame->Hide();

    codium::CodeBlocksSdkBootstrap bootstrap(frame);
    wxString error;
    if (!bootstrap.Start(wxString::FromUTF8(argv[2]), wxString::FromUTF8(argv[3]), &error)) {
        std::cerr << "codeblocks-sdk-events-smoke: bootstrap failed: " << error.ToStdString() << "\n";
        while (frame->GetEventHandler() != frame) frame->PopEventHandler(true);
        delete frame;
        wxTheApp->OnExit();
        wxEntryCleanup();
        return 2;
    }
    cbProject* project = bootstrap.LoadProject(wxString::FromUTF8(argv[1]), &error);
    const auto projectEvents = bootstrap.DrainEvents();
    if (!project || !HasEvent(projectEvents, codium::CodeBlocksEventKind::ProjectOpened,
                              wxS("cbEVT_PROJECT_OPEN"))) {
        std::cerr << "codeblocks-sdk-events-smoke: official project-open event missing\n";
        bootstrap.Shutdown();
        while (frame->GetEventHandler() != frame) frame->PopEventHandler(true);
        delete frame;
        wxTheApp->OnExit();
        wxEntryCleanup();
        return 3;
    }

    cbPlugin* compiler = Manager::Get()->GetPluginManager()->FindPluginByName(wxS("Compiler"));
    if (!compiler) {
        std::cerr << "codeblocks-sdk-events-smoke: Compiler plugin was not registered\n";
        bootstrap.Shutdown();
        while (frame->GetEventHandler() != frame) frame->PopEventHandler(true);
        delete frame;
        wxTheApp->OnExit();
        wxEntryCleanup();
        return 4;
    }

    CodeBlocksEvent compilerStarted(cbEVT_COMPILER_STARTED, 0, project, nullptr, compiler);
    Manager::Get()->ProcessEvent(compilerStarted);
    CodeBlocksEvent compilerFinished(cbEVT_COMPILER_FINISHED, 0, project, nullptr, compiler);
    compilerFinished.SetInt(17);
    Manager::Get()->ProcessEvent(compilerFinished);

    CodeBlocksEvent fileAdded(cbEVT_PROJECT_FILE_ADDED);
    fileAdded.SetProject(project);
    fileAdded.SetString(wxS("src/main.cpp"));
    Manager::Get()->ProcessEvent(fileAdded);

    const auto normalized = bootstrap.DrainEvents();
    bool failedBuild = false;
    bool compilerPlugin = false;
    bool addedFile = false;
    for (const auto& event : normalized) {
        if (event.kind == codium::CodeBlocksEventKind::BuildFinished &&
            event.exitCode == 17 && event.isError) {
            failedBuild = true;
        }
        if (event.kind == codium::CodeBlocksEventKind::BuildStarted &&
            event.plugin == wxS("Compiler")) {
            compilerPlugin = true;
        }
        if (event.kind == codium::CodeBlocksEventKind::ProjectFileAdded &&
            event.filePath == wxS("src/main.cpp")) {
            addedFile = true;
        }
    }

    bootstrap.Shutdown();
    while (frame->GetEventHandler() != frame) frame->PopEventHandler(true);
    delete frame;
    wxTheApp->OnExit();
    wxEntryCleanup();

    if (!failedBuild || !compilerPlugin || !addedFile) {
        std::cerr << "codeblocks-sdk-events-smoke: official event normalization failed\n";
        return 5;
    }

    std::cout << "codeblocks-sdk-events-smoke: ok — official project/compiler events normalized\n";
    return 0;
}
