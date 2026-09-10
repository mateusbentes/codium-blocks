#include "codium/codeblocks_sdk_bootstrap.hpp"

#include <wx/app.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/fs_mem.h>
#include <wx/fs_zip.h>
#include <wx/frame.h>
#include <wx/image.h>
#include <wx/xrc/xmlres.h>

#include <cbplugin.h>
#include <cbproject.h>
#include <configmanager.h>
#include <manager.h>
#include <pluginmanager.h>
#include <projectbuildtarget.h>
#include <projectmanager.h>

#include <utility>

namespace codium {
namespace {

constexpr int kSdkMajor = PLUGIN_SDK_VERSION_MAJOR;
constexpr int kSdkMinor = PLUGIN_SDK_VERSION_MINOR;
constexpr int kSdkRelease = PLUGIN_SDK_VERSION_RELEASE;

void RegisterWxResources()
{
    wxFileSystem::AddHandler(new wxZipFSHandler);
    wxFileSystem::AddHandler(new wxMemoryFSHandler);
    wxInitAllImageHandlers();
    wxXmlResource::Get()->InitAllHandlers();
}

} // namespace

CodeBlocksSdkBootstrap::CodeBlocksSdkBootstrap(wxFrame* appFrame)
    : appFrame_(appFrame)
{
    report_.sdkMajor = kSdkMajor;
    report_.sdkMinor = kSdkMinor;
    report_.sdkRelease = kSdkRelease;
    report_.sdkIdentity = wxString::Format(
        wxS("Code::Blocks SDK %d.%d.%d"), kSdkMajor, kSdkMinor, kSdkRelease);
}

CodeBlocksSdkBootstrap::~CodeBlocksSdkBootstrap()
{
    Shutdown();
}

void CodeBlocksSdkBootstrap::Fail(const wxString& message, wxString* error)
{
    if (error) *error = message;
}

bool CodeBlocksSdkBootstrap::Start(const wxString& dataDirectory,
                                   const wxString& compilerPlugin,
                                   wxString* error)
{
    if (started_) return true;
    if (!wxTheApp || !appFrame_) {
        Fail(wxS("The Code::Blocks adapter requires an initialized wxApp and frame."), error);
        return false;
    }
    if (!wxDirExists(dataDirectory)) {
        Fail(wxString::Format(wxS("Code::Blocks data directory does not exist: %s"), dataDirectory), error);
        return false;
    }
    if (!wxFileExists(compilerPlugin)) {
        Fail(wxString::Format(wxS("The allowlisted Code::Blocks Compiler plugin does not exist: %s"),
                              compilerPlugin), error);
        return false;
    }

    // This is the official Code::Blocks data-path override. It must be set
    // before Manager::Get() so the SDK can resolve its manager resources.
    wxSetEnv(wxS("CODEBLOCKS_DATA_DIR"), dataDirectory);
    RegisterWxResources();

    Manager* manager = Manager::Get(appFrame_);
    if (!manager) {
        Fail(wxS("Code::Blocks Manager initialization returned null."), error);
        return false;
    }

    manager->GetConfigManager(wxS("app"))->Write(wxS("data_path"), dataDirectory);
    report_.dataDirectory = dataDirectory;
    report_.wxAppReady = true;

    if (!Manager::LoadResource(wxS("resources.zip"))) {
        Fail(wxString::Format(wxS("Code::Blocks resources.zip could not be loaded from %s."),
                              dataDirectory), error);
        Manager::Free();
        return false;
    }
    report_.resourcesLoaded = true;

    // ProjectManager::BeginLoadingProject requires the matching Compiler
    // plugin to be present. Load exactly the caller-selected plugin path;
    // never scan a directory and never load third-party plugins implicitly.
    PluginManager* plugins = manager->GetPluginManager();
    if (!plugins->LoadPlugin(compilerPlugin)) {
        Fail(wxString::Format(wxS("The Code::Blocks Compiler plugin could not be loaded: %s"),
                              compilerPlugin), error);
        Manager::Free();
        return false;
    }
    if (!plugins->FindPluginByName(wxS("Compiler"))) {
        Fail(wxS("The selected native plugin did not register as Code::Blocks Compiler."), error);
        Manager::Free();
        return false;
    }

    report_.compilerPlugin = compilerPlugin;
    report_.compilerPluginLoaded = true;
    report_.projectEnumerationAvailable = true;
    Manager::SetBatchBuild(true);
    Manager::SetAppStartedUp(true);
    started_ = true;
    return true;
}

cbProject* CodeBlocksSdkBootstrap::LoadProject(const wxString& projectFile,
                                               wxString* error)
{
    if (!started_) {
        Fail(wxS("The Code::Blocks SDK bootstrap is not running."), error);
        return nullptr;
    }
    if (!wxFileExists(projectFile)) {
        Fail(wxString::Format(wxS("Code::Blocks project does not exist: %s"), projectFile), error);
        return nullptr;
    }

    ProjectManager* projectManager = Manager::Get()->GetProjectManager();
    if (!projectManager) {
        Fail(wxS("Code::Blocks ProjectManager is unavailable."), error);
        return nullptr;
    }
    if (project_ && !projectManager->CloseAllProjects(true)) {
        Fail(wxS("The previous Code::Blocks project could not be closed."), error);
        return nullptr;
    }
    project_ = projectManager->LoadProject(projectFile, true);
    if (!project_) {
        Fail(wxString::Format(wxS("Code::Blocks could not load project: %s"), projectFile), error);
        return nullptr;
    }
    return project_;
}

std::vector<CodeBlocksTargetInfo> CodeBlocksSdkBootstrap::EnumerateTargets(cbProject* project) const
{
    std::vector<CodeBlocksTargetInfo> targets;
    if (!project) return targets;
    for (int index = 0; index < project->GetBuildTargetsCount(); ++index) {
        ProjectBuildTarget* target = project->GetBuildTarget(index);
        if (!target) continue;
        CodeBlocksTargetInfo info;
        info.title = target->GetTitle();
        info.compilerId = target->GetCompilerID();
        info.outputPath = target->GetOutputFilename();
        info.workingDirectory = target->GetWorkingDir();
        targets.push_back(std::move(info));
    }
    return targets;
}

void CodeBlocksSdkBootstrap::Shutdown()
{
    if (!started_) return;
    ProjectManager* projectManager = Manager::Get()->GetProjectManager();
    if (projectManager) projectManager->CloseAllProjects(true);
    Manager::SetAppShuttingDown(true);
    Manager::Free();
    project_ = nullptr;
    started_ = false;
}

} // namespace codium
