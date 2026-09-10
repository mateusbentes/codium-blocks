#include "codium/codeblocks_sdk_bootstrap.hpp"
#include "codium/codeblocks_debugger_headless.hpp"

#include <wx/app.h>
#include <wx/dir.h>
#include <wx/eventfilter.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/fs_mem.h>
#include <wx/fs_zip.h>
#include <wx/frame.h>
#include <wx/image.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/xrc/xmlres.h>
#include <wx/xml/xml.h>

#include <cbplugin.h>
#include <cbproject.h>
#include <configmanager.h>
#include <debuggermanager.h>
#include <manager.h>
#include <pluginmanager.h>
#include <projectbuildtarget.h>
#include <projectmanager.h>
#include <sdk_events.h>

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

wxString ProjectPath(CodeBlocksEvent& event)
{
    cbProject* project = event.GetProject();
    return project ? project->GetFilename() : wxString();
}

wxString ActiveTarget(CodeBlocksEvent& event)
{
    cbProject* project = event.GetProject();
    if (!project) return event.GetBuildTargetName();
    const wxString& active = project->GetActiveBuildTarget();
    return active.empty() ? event.GetBuildTargetName() : active;
}

wxFileName NormalizedPath(const wxString& path)
{
    wxFileName result(path);
    result.MakeAbsolute();
    result.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    return result;
}

bool SamePath(const wxString& left, const wxString& right)
{
    const wxString lhs = NormalizedPath(left).GetFullPath();
    const wxString rhs = NormalizedPath(right).GetFullPath();
#ifdef _WIN32
    return lhs.CmpNoCase(rhs) == 0;
#else
    return lhs == rhs;
#endif
}

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

bool CopyAllowlistedPlugin(const wxString& source,
                           const wxString& stagingDirectory,
                           wxString* stagedPath,
                           wxString* error)
{
    const wxFileName sourceFile = NormalizedPath(source);
    if (!sourceFile.FileExists()) {
        if (error) *error = wxString::Format(wxS("Allowlisted Code::Blocks plugin does not exist: %s"), source);
        return false;
    }
    const wxFileName destination(stagingDirectory, sourceFile.GetFullName());
    if (!wxCopyFile(sourceFile.GetFullPath(), destination.GetFullPath(), true)) {
        if (error) *error = wxString::Format(wxS("Could not stage Code::Blocks plugin: %s"), source);
        return false;
    }
    if (stagedPath) *stagedPath = destination.GetFullPath();
    return true;
}

wxString DebuggerEventPlugin(CodeBlocksEvent& event, cbDebuggerPlugin* debugger)
{
    return event.GetPlugin() == debugger ? wxString(wxS("Debugger")) : wxString(wxS("Code::Blocks"));
}

} // namespace

class CodeBlocksCompilerOutputFilter final : public wxEventFilter
{
public:
    explicit CodeBlocksCompilerOutputFilter(CodeBlocksSdkBootstrap* owner)
        : owner_(owner)
    {
    }

    int FilterEvent(wxEvent& event) override
    {
        if (!owner_) return Event_Skip;
        if (event.GetEventType() != cbEVT_PIPEDPROCESS_STDOUT &&
            event.GetEventType() != cbEVT_PIPEDPROCESS_STDERR) {
            return Event_Skip;
        }
        auto* codeBlocksEvent = dynamic_cast<CodeBlocksEvent*>(&event);
        if (!codeBlocksEvent) return Event_Skip;
        if (event.GetEventType() == cbEVT_PIPEDPROCESS_STDERR)
            owner_->OnCompilerError(*codeBlocksEvent);
        else
            owner_->OnCompilerOutput(*codeBlocksEvent);
        return Event_Skip;
    }

private:
    CodeBlocksSdkBootstrap* owner_ = nullptr;
};

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

void CodeBlocksSdkBootstrap::RegisterEventSinks()
{
    if (eventSinksRegistered_) return;
    Manager* manager = Manager::Get();
    const wxEventType projectEvents[] = {
        cbEVT_PROJECT_OPEN,
        cbEVT_PROJECT_CLOSE,
        cbEVT_PROJECT_ACTIVATE,
        cbEVT_PROJECT_SAVE,
        cbEVT_PROJECT_TARGETS_MODIFIED,
        cbEVT_PROJECT_FILE_ADDED,
        cbEVT_PROJECT_FILE_REMOVED,
        cbEVT_PROJECT_FILE_CHANGED,
        cbEVT_PROJECT_FILE_RENAMED
    };
    for (const wxEventType eventType : projectEvents) {
        manager->RegisterEventSink(
            eventType,
            new cbEventFunctor<CodeBlocksSdkBootstrap, CodeBlocksEvent>(
                this, &CodeBlocksSdkBootstrap::OnSdkEvent));
    }
    const wxEventType compilerEvents[] = {
        cbEVT_COMPILER_STARTED,
        cbEVT_COMPILER_FINISHED
    };
    for (const wxEventType eventType : compilerEvents) {
        manager->RegisterEventSink(
            eventType,
            new cbEventFunctor<CodeBlocksSdkBootstrap, CodeBlocksEvent>(
                this, &CodeBlocksSdkBootstrap::OnSdkEvent));
    }
    const wxEventType debuggerEvents[] = {
        cbEVT_DEBUGGER_STARTED,
        cbEVT_DEBUGGER_PAUSED,
        cbEVT_DEBUGGER_CONTINUED,
        cbEVT_DEBUGGER_FINISHED,
        cbEVT_DEBUGGER_CURSOR_CHANGED,
        cbEVT_DEBUGGER_UPDATED
    };
    for (const wxEventType eventType : debuggerEvents) {
        manager->RegisterEventSink(
            eventType,
            new cbEventFunctor<CodeBlocksSdkBootstrap, CodeBlocksEvent>(
                this, &CodeBlocksSdkBootstrap::OnSdkEvent));
    }
    eventSinksRegistered_ = true;
    report_.eventSinkRegistered = true;
    report_.compilerEventsAvailable = true;
    report_.debuggerEventsAvailable = true;
}

void CodeBlocksSdkBootstrap::UnregisterEventSinks()
{
    if (!eventSinksRegistered_) return;
    Manager::Get()->RemoveAllEventSinksFor(this);
    eventSinksRegistered_ = false;
    report_.eventSinkRegistered = false;
    report_.compilerEventsAvailable = false;
    report_.debuggerEventsAvailable = false;
}

void CodeBlocksSdkBootstrap::BindCompilerOutput()
{
    if (!compilerPlugin_ || report_.compilerOutputAvailable) return;
    compilerOutputFilter_ = std::make_unique<CodeBlocksCompilerOutputFilter>(this);
    wxEvtHandler::AddFilter(compilerOutputFilter_.get());
    report_.compilerOutputAvailable = true;
}

void CodeBlocksSdkBootstrap::UnbindCompilerOutput()
{
    if (!compilerPlugin_ || !report_.compilerOutputAvailable) return;
    wxEvtHandler::RemoveFilter(compilerOutputFilter_.get());
    compilerOutputFilter_.reset();
    report_.compilerOutputAvailable = false;
}

void CodeBlocksSdkBootstrap::RemovePluginStaging()
{
    if (pluginStagingDirectory_.empty()) return;
    wxFileName::Rmdir(pluginStagingDirectory_, wxPATH_RMDIR_RECURSIVE);
    pluginStagingDirectory_.clear();
}

void CodeBlocksSdkBootstrap::PublishSdkEvent(CodeBlocksHostEvent event)
{
    events_.push_back(std::move(event));
}

void CodeBlocksSdkBootstrap::OnSdkEvent(CodeBlocksEvent& event)
{
    CodeBlocksHostEvent normalized;
    normalized.projectPath = ProjectPath(event);
    normalized.target = ActiveTarget(event);
    normalized.payload = wxString::Format(wxS("sdkEventType=%lu"),
                                          static_cast<unsigned long>(event.GetEventType()));

    if (event.GetEventType() == cbEVT_PROJECT_OPEN) {
        normalized.kind = CodeBlocksEventKind::ProjectOpened;
        normalized.message = wxS("Code::Blocks emitted cbEVT_PROJECT_OPEN");
    } else if (event.GetEventType() == cbEVT_PROJECT_CLOSE) {
        normalized.kind = CodeBlocksEventKind::ProjectClosed;
        normalized.message = wxS("Code::Blocks emitted cbEVT_PROJECT_CLOSE");
    } else if (event.GetEventType() == cbEVT_PROJECT_ACTIVATE) {
        normalized.kind = CodeBlocksEventKind::ProjectActivated;
        normalized.message = wxS("Code::Blocks emitted cbEVT_PROJECT_ACTIVATE");
    } else if (event.GetEventType() == cbEVT_PROJECT_SAVE) {
        normalized.kind = CodeBlocksEventKind::ProjectSaved;
        normalized.message = wxS("Code::Blocks emitted cbEVT_PROJECT_SAVE");
    } else if (event.GetEventType() == cbEVT_PROJECT_TARGETS_MODIFIED) {
        normalized.kind = CodeBlocksEventKind::ProjectTargetsChanged;
        normalized.message = wxS("Code::Blocks emitted cbEVT_PROJECT_TARGETS_MODIFIED");
    } else if (event.GetEventType() == cbEVT_PROJECT_FILE_ADDED) {
        normalized.kind = CodeBlocksEventKind::ProjectFileAdded;
        normalized.filePath = event.GetString();
        normalized.message = wxS("Code::Blocks emitted cbEVT_PROJECT_FILE_ADDED");
    } else if (event.GetEventType() == cbEVT_PROJECT_FILE_REMOVED) {
        normalized.kind = CodeBlocksEventKind::ProjectFileRemoved;
        normalized.filePath = event.GetString();
        normalized.message = wxS("Code::Blocks emitted cbEVT_PROJECT_FILE_REMOVED");
    } else if (event.GetEventType() == cbEVT_PROJECT_FILE_CHANGED) {
        normalized.kind = CodeBlocksEventKind::ProjectFileChanged;
        normalized.filePath = event.GetString();
        normalized.message = wxS("Code::Blocks emitted cbEVT_PROJECT_FILE_CHANGED");
    } else if (event.GetEventType() == cbEVT_PROJECT_FILE_RENAMED) {
        normalized.kind = CodeBlocksEventKind::ProjectFileRenamed;
        normalized.filePath = event.GetNewFileName();
        normalized.oldFilePath = event.GetOldFileName();
        normalized.message = wxS("Code::Blocks emitted cbEVT_PROJECT_FILE_RENAMED");
    } else if (event.GetEventType() == cbEVT_COMPILER_STARTED) {
        normalized.kind = CodeBlocksEventKind::BuildStarted;
        normalized.plugin = wxS("Compiler");
        normalized.message = wxS("Code::Blocks emitted cbEVT_COMPILER_STARTED");
    } else if (event.GetEventType() == cbEVT_COMPILER_FINISHED) {
        normalized.kind = CodeBlocksEventKind::BuildFinished;
        normalized.plugin = wxS("Compiler");
        normalized.exitCode = event.GetInt();
        normalized.isError = normalized.exitCode != 0;
        normalized.message = normalized.isError
            ? wxS("Code::Blocks emitted cbEVT_COMPILER_FINISHED with failure")
            : wxS("Code::Blocks emitted cbEVT_COMPILER_FINISHED");
    } else if (event.GetEventType() == cbEVT_DEBUGGER_STARTED) {
        normalized.kind = CodeBlocksEventKind::DebugSessionStarted;
        normalized.plugin = DebuggerEventPlugin(event, debuggerPlugin_);
        normalized.message = wxS("Code::Blocks emitted cbEVT_DEBUGGER_STARTED");
    } else if (event.GetEventType() == cbEVT_DEBUGGER_PAUSED) {
        normalized.kind = CodeBlocksEventKind::DebugSessionPaused;
        normalized.plugin = DebuggerEventPlugin(event, debuggerPlugin_);
        normalized.message = wxS("Code::Blocks emitted cbEVT_DEBUGGER_PAUSED");
    } else if (event.GetEventType() == cbEVT_DEBUGGER_CONTINUED) {
        normalized.kind = CodeBlocksEventKind::DebugSessionContinued;
        normalized.plugin = DebuggerEventPlugin(event, debuggerPlugin_);
        normalized.message = wxS("Code::Blocks emitted cbEVT_DEBUGGER_CONTINUED");
    } else if (event.GetEventType() == cbEVT_DEBUGGER_FINISHED) {
        normalized.kind = CodeBlocksEventKind::DebugSessionStopped;
        normalized.plugin = DebuggerEventPlugin(event, debuggerPlugin_);
        normalized.exitCode = debuggerPlugin_ ? debuggerPlugin_->GetExitCode() : event.GetInt();
        normalized.isError = normalized.exitCode != 0;
        normalized.message = wxS("Code::Blocks emitted cbEVT_DEBUGGER_FINISHED");
    } else if (event.GetEventType() == cbEVT_DEBUGGER_CURSOR_CHANGED) {
        normalized.kind = CodeBlocksEventKind::DebugSessionCursorChanged;
        normalized.plugin = DebuggerEventPlugin(event, debuggerPlugin_);
        normalized.line = event.GetInt();
        normalized.column = event.GetExtraLong();
        normalized.message = wxS("Code::Blocks emitted cbEVT_DEBUGGER_CURSOR_CHANGED");
    } else if (event.GetEventType() == cbEVT_DEBUGGER_UPDATED) {
        normalized.kind = CodeBlocksEventKind::DebugSessionUpdated;
        normalized.plugin = DebuggerEventPlugin(event, debuggerPlugin_);
        normalized.exitCode = event.GetInt();
        normalized.message = wxS("Code::Blocks emitted cbEVT_DEBUGGER_UPDATED");
    } else {
        return;
    }
    PublishSdkEvent(std::move(normalized));
}

void CodeBlocksSdkBootstrap::OnCompilerOutput(CodeBlocksEvent& event)
{
    if (event.GetString().empty()) return;
    CodeBlocksHostEvent normalized;
    normalized.kind = CodeBlocksEventKind::CompilerOutput;
    normalized.projectPath = project_ ? project_->GetFilename() : wxString();
    normalized.target = project_ ? project_->GetActiveBuildTarget() : wxString();
    normalized.plugin = wxS("Compiler");
    normalized.message = event.GetString();
    normalized.payload = event.GetString();
    PublishSdkEvent(std::move(normalized));
}

void CodeBlocksSdkBootstrap::OnCompilerError(CodeBlocksEvent& event)
{
    if (event.GetString().empty()) return;
    CodeBlocksHostEvent normalized;
    normalized.kind = CodeBlocksEventKind::CompilerOutput;
    normalized.projectPath = project_ ? project_->GetFilename() : wxString();
    normalized.target = project_ ? project_->GetActiveBuildTarget() : wxString();
    normalized.plugin = wxS("Compiler");
    normalized.message = event.GetString();
    normalized.payload = event.GetString();
    normalized.isError = true;
    PublishSdkEvent(std::move(normalized));
}

bool CodeBlocksSdkBootstrap::Start(const wxString& dataDirectory,
                                   const wxString& compilerPlugin,
                                   wxString* error,
                                   const wxString& debuggerPlugin,
                                   const wxString& pluginDirectory,
                                   bool workspaceTrusted)
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
    if (!debuggerPlugin.empty() && !wxFileExists(debuggerPlugin)) {
        Fail(wxString::Format(wxS("The allowlisted Code::Blocks Debugger plugin does not exist: %s"),
                              debuggerPlugin), error);
        return false;
    }
    if (!workspaceTrusted && !debuggerPlugin.empty()) {
        Fail(wxS("Loading a native Code::Blocks Debugger plugin requires a trusted workspace."), error);
        return false;
    }
    if (!debuggerPlugin.empty() &&
        !SamePath(wxFileName(compilerPlugin).GetPath(), wxFileName(debuggerPlugin).GetPath())) {
        Fail(wxS("The Compiler and Debugger plugins must come from the same Code::Blocks plugin directory."), error);
        return false;
    }

    // This is the official Code::Blocks data-path override. It must be set
    // before Manager::Get() so the SDK can resolve its manager resources.
    wxSetEnv(wxS("CODEBLOCKS_DATA_DIR"), dataDirectory);
    RegisterWxResources();

    Manager* manager = Manager::Get();
    if (!manager) {
        Fail(wxS("Code::Blocks Manager initialization returned null."), error);
        return false;
    }
    manager->GetConfigManager(wxS("app"))->Write(wxS("data_path"), dataDirectory);
    Manager::SetBatchBuild(true);
    manager = Manager::Get(appFrame_);
    RegisterEventSinks();

    report_.dataDirectory = dataDirectory;
    report_.workspaceTrusted = workspaceTrusted;
    report_.pluginDirectory = pluginDirectory.empty()
        ? wxFileName(compilerPlugin).GetPath()
        : pluginDirectory;
    report_.pluginPolicy = debuggerPlugin.empty()
        ? wxS("Only the explicitly selected Compiler plugin is loaded.")
        : wxS("Only the explicitly selected Compiler and Debugger plugins from one matched directory are loaded.");
    report_.wxAppReady = true;

    if (!Manager::LoadResource(wxS("resources.zip"))) {
        Fail(wxString::Format(wxS("Code::Blocks resources.zip could not be loaded from %s."),
                              dataDirectory), error);
        UnregisterEventSinks();
        Manager::Free();
        return false;
    }
    report_.resourcesLoaded = true;

    // ProjectManager::BeginLoadingProject requires the matching Compiler
    // plugin to be present. With a debugger requested, stage exactly the two
    // selected files so Code::Blocks can keep one manifest context per scan;
    // never scan the user's complete plugin directory.
    PluginManager* plugins = manager->GetPluginManager();
    wxString loadedCompilerPlugin = compilerPlugin;
    wxString loadedDebuggerPlugin = debuggerPlugin;
    if (!debuggerPlugin.empty()) {
        pluginStagingDirectory_ = wxStandardPaths::Get().GetTempDir() +
            wxString::Format(wxS("/codium-blocks-codeblocks-%lu"), wxGetProcessId());
        if (!wxMkdir(pluginStagingDirectory_) && !wxDirExists(pluginStagingDirectory_)) {
            Fail(wxString::Format(wxS("Could not create Code::Blocks plugin staging directory: %s"),
                                  pluginStagingDirectory_), error);
            UnregisterEventSinks();
            Manager::Free();
            return false;
        }
        Manager::SetBatchBuild(false);
        if (!CopyAllowlistedPlugin(compilerPlugin, pluginStagingDirectory_, &loadedCompilerPlugin, error) ||
            !CopyAllowlistedPlugin(debuggerPlugin, pluginStagingDirectory_, &loadedDebuggerPlugin, error) ||
            plugins->ScanForPlugins(pluginStagingDirectory_) < 2) {
            if (error && error->empty()) *error = wxS("The selected Code::Blocks plugins could not be staged and scanned.");
            wxFileName::Rmdir(pluginStagingDirectory_, wxPATH_RMDIR_RECURSIVE);
            pluginStagingDirectory_.clear();
            Manager::SetBatchBuild(true);
            UnregisterEventSinks();
            Manager::Free();
            return false;
        }
        Manager::SetBatchBuild(true);
    }
    if (debuggerPlugin.empty() && !plugins->LoadPlugin(loadedCompilerPlugin)) {
        Fail(wxString::Format(wxS("The Code::Blocks Compiler plugin could not be loaded: %s"),
                              compilerPlugin), error);
        UnregisterEventSinks();
        Manager::Free();
        return false;
    }
    if (!debuggerPlugin.empty()) {
        cbPlugin* stagedCompiler = plugins->FindPluginByFileName(loadedCompilerPlugin);
        cbPlugin* stagedDebugger = plugins->FindPluginByFileName(loadedDebuggerPlugin);
        if (!stagedCompiler || !stagedDebugger) {
            Fail(wxS("The allowlisted Code::Blocks plugins could not be attached from staging."), error);
            UnregisterEventSinks();
            Manager::Free();
            RemovePluginStaging();
            return false;
        }
    }

    // CompilerGCC registers its CompilerFactory entries in OnAttach(), but
    // LoadSettings() follows immediately and can open AutoDetectCompilers for
    // a fresh profile. Seed only the standard GCC entry before AttachPlugin;
    // existing user values remain authoritative. Windows and macOS adapters
    // will supply their own toolchain profile policy later.
    ConfigManager* compilerConfig = manager->GetConfigManager(wxS("compiler"));
    if (compilerConfig->Read(wxString(wxS("settings_version"))).empty())
        compilerConfig->Write(wxString(wxS("settings_version")), wxString(wxS("0.0.3")), false);
    const auto seedCompiler = [compilerConfig](const wxString& id, const wxString& name) {
        const wxString base = wxString(wxS("/sets/")) + id;
        if (compilerConfig->Read(base + wxString(wxS("/name"))).empty())
            compilerConfig->Write(base + wxString(wxS("/name")), name, false);
        if (compilerConfig->Read(base + wxString(wxS("/master_path"))).empty())
            compilerConfig->Write(base + wxString(wxS("/master_path")), wxString(wxS("/usr")), false);
    };
    if (wxFileExists(wxS("/usr/bin/gcc"))) {
        seedCompiler(wxString(wxS("gcc")), wxString(wxS("GNU GCC Compiler")));
        seedCompiler(wxString(wxS("icc")), wxString(wxS("Intel C/C++ Compiler")));
        seedCompiler(wxString(wxS("gdc")), wxString(wxS("GDC D Compiler")));
        seedCompiler(wxString(wxS("gfortran")), wxString(wxS("GNU Fortran Compiler")));
        seedCompiler(wxString(wxS("g95")), wxString(wxS("G95 Fortran Compiler")));
        seedCompiler(wxString(wxS("arm-elf-gcc")), wxString(wxS("GNU GCC Compiler for ARM")));

        wxDir compilerDirectory(dataDirectory + wxString(wxS("/compilers")));
        wxString compilerFile;
        bool hasCompilerFile = compilerDirectory.GetFirst(&compilerFile, wxS("compiler_*.xml"), wxDIR_FILES);
        while (hasCompilerFile) {
            wxXmlDocument compilerDefinition(dataDirectory + wxString(wxS("/compilers/")) + compilerFile);
            wxXmlNode* root = compilerDefinition.GetRoot();
            if (root && root->GetName() == wxS("CodeBlocks_compiler")) {
                wxString platform;
                const wxString id = root->GetAttribute(wxS("id"), wxEmptyString);
                const wxString name = root->GetAttribute(wxS("name"), wxEmptyString);
                const bool platformMatches =
                    !root->GetAttribute(wxS("platform"), &platform) || platform == wxS("linux") || platform == wxS("unix");
                if (platformMatches && !id.empty() && !name.empty())
                    seedCompiler(id, name);
            }
            hasCompilerFile = compilerDirectory.GetNext(&compilerFile);
        }
    }
    cbPlugin* loadedPlugin = plugins->FindPluginByName(wxS("Compiler"));
    if (!loadedPlugin) {
        Fail(wxS("The selected native plugin did not register as Code::Blocks Compiler."), error);
        UnregisterEventSinks();
        Manager::Free();
        RemovePluginStaging();
        return false;
    }
    compilerPlugin_ = dynamic_cast<cbCompilerPlugin*>(loadedPlugin);
    if (!compilerPlugin_) {
        Fail(wxS("The selected Code::Blocks plugin is not a compiler plugin."), error);
        UnregisterEventSinks();
        Manager::Free();
        RemovePluginStaging();
        return false;
    }
    if (!plugins->AttachPlugin(compilerPlugin_, true)) {
        Fail(wxS("The matched Code::Blocks Compiler plugin could not be attached."), error);
        compilerPlugin_ = nullptr;
        UnregisterEventSinks();
        Manager::Free();
        RemovePluginStaging();
        return false;
    }
    report_.compilerPlugin = compilerPlugin;
    report_.compilerPluginLoaded = true;
    report_.compilerPluginAttached = true;
    BindCompilerOutput();
    report_.projectEnumerationAvailable = true;
    Manager::SetAppStartedUp(true);

    if (!debuggerPlugin.empty()) {
        DebuggerManager* debuggerManager = manager->GetDebuggerManager();
        if (!debuggerManager) {
            Fail(wxS("The Code::Blocks DebuggerManager is unavailable."), error);
            UnregisterEventSinks();
            Manager::Free();
            RemovePluginStaging();
            return false;
        }
        debuggerManager->SetInterfaceFactory(CreateHeadlessDebuggerInterfaceFactory());
        debuggerManager->SetMenuHandler(CreateHeadlessDebuggerMenuHandler());

        cbPlugin* loadedDebugger = plugins->FindPluginByFileName(loadedDebuggerPlugin);
        if (!loadedDebugger) loadedDebugger = plugins->FindPluginByName(wxS("Debugger"));
        debuggerPlugin_ = dynamic_cast<cbDebuggerPlugin*>(loadedDebugger);
        if (!debuggerPlugin_ || !plugins->AttachPlugin(debuggerPlugin_, true)) {
            Fail(wxS("The matched Code::Blocks Debugger plugin could not be attached."), error);
            debuggerPlugin_ = nullptr;
            UnregisterEventSinks();
            Manager::Free();
            RemovePluginStaging();
            return false;
        }
        report_.debuggerPlugin = debuggerPlugin;
        report_.debuggerPluginLoaded = true;
        report_.debuggerPluginAttached = true;
        report_.debuggerControlAvailable = true;
        report_.debuggerSnapshotAvailable = true;
        report_.debuggerPublicStateAvailable = true;
        report_.debuggerPrivateDataAvailable = false;
    }
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

bool CodeBlocksSdkBootstrap::BuildProject(const wxString& projectFile,
                                          const wxString& target,
                                          const wxString& configuration,
                                          wxString* error)
{
    if (!started_ || !compilerPlugin_) {
        Fail(wxS("The Code::Blocks Compiler plugin is not available."), error);
        return false;
    }
    if (compilerPlugin_->IsRunning()) {
        Fail(wxS("A Code::Blocks build is already running."), error);
        return false;
    }

    if (!project_ || project_->GetFilename() != projectFile) {
        if (!LoadProject(projectFile, error)) return false;
    }
    if (!project_) {
        Fail(wxS("Code::Blocks project is not loaded."), error);
        return false;
    }

    wxString buildTarget = target;
    if (buildTarget.empty() || buildTarget.CmpNoCase(wxS("all")) == 0) {
        buildTarget = configuration.empty() ? project_->GetFirstValidBuildTargetName()
                                            : configuration;
    }
    if (buildTarget.empty()) {
        Fail(wxS("No Code::Blocks build target was selected."), error);
        return false;
    }
    if (!project_->BuildTargetValid(buildTarget, false)) {
        Fail(wxString::Format(wxS("Code::Blocks build target does not exist: %s"), buildTarget), error);
        return false;
    }

    const int result = compilerPlugin_->Build(buildTarget);
    // CompilerGCC returns -3 when its asynchronous command queue is already
    // advancing to the next process. The build has still been accepted; only
    // -1/-2 represent a rejected or invalid build request here.
    if (result == -1 || result == -2) {
        Fail(wxString::Format(wxS("Code::Blocks Compiler rejected build target %s (status %d)."),
                              buildTarget, result), error);
        return false;
    }
    return true;
}

bool CodeBlocksSdkBootstrap::StartDebug(const wxString& projectFile,
                                        const wxString& target,
                                        bool breakOnEntry,
                                        wxString* error)
{
    if (!started_ || !debuggerPlugin_) {
        Fail(wxS("The Code::Blocks Debugger plugin is not available."), error);
        return false;
    }
    if (debuggerPlugin_->IsRunning()) {
        Fail(wxS("A Code::Blocks debug session is already running."), error);
        return false;
    }
    if (!project_ || project_->GetFilename() != projectFile) {
        if (!LoadProject(projectFile, error)) return false;
    }
    if (!project_) {
        Fail(wxS("Code::Blocks project is not loaded for debugging."), error);
        return false;
    }
    if (!target.empty() && target.CmpNoCase(wxS("all")) != 0) {
        if (!project_->BuildTargetValid(target, false)) {
            Fail(wxString::Format(wxS("Code::Blocks debug target does not exist: %s"), target), error);
            return false;
        }
        if (!project_->SetActiveBuildTarget(target)) {
            Fail(wxString::Format(wxS("Code::Blocks could not select debug target: %s"), target), error);
            return false;
        }
    }
    if (!debuggerPlugin_->Debug(breakOnEntry)) {
        Fail(wxS("The Code::Blocks Debugger plugin rejected the debug request."), error);
        return false;
    }
    return true;
}

bool CodeBlocksSdkBootstrap::ContinueDebug(wxString* error)
{
    if (!debuggerPlugin_ || !debuggerPlugin_->IsRunning()) {
        Fail(wxS("No Code::Blocks debug session is running."), error);
        return false;
    }
    debuggerPlugin_->Continue();
    return true;
}

bool CodeBlocksSdkBootstrap::PauseDebug(wxString* error)
{
    if (!debuggerPlugin_ || !debuggerPlugin_->IsRunning()) {
        Fail(wxS("No Code::Blocks debug session is running."), error);
        return false;
    }
    debuggerPlugin_->Break();
    return true;
}

bool CodeBlocksSdkBootstrap::StopDebug(wxString* error)
{
    if (!debuggerPlugin_ || !debuggerPlugin_->IsRunning()) {
        Fail(wxS("No Code::Blocks debug session is running."), error);
        return false;
    }
    debuggerPlugin_->Stop();
    return true;
}

void CodeBlocksSdkBootstrap::PublishDebugSnapshot(const wxString& dataKind)
{
    if (!debuggerPlugin_) return;

    const bool running = debuggerPlugin_->IsRunning();
    const bool stopped = debuggerPlugin_->IsStopped();
    const bool busy = debuggerPlugin_->IsBusy();
    wxString currentFile;
    int currentLine = 0;
    int activeFrame = 0;
    int breakpointCount = 0;
    if (stopped) {
        debuggerPlugin_->GetCurrentPosition(currentFile, currentLine);
        activeFrame = debuggerPlugin_->GetActiveStackFrame();
    }
    if (debuggerPlugin_->SupportsFeature(cbDebuggerFeature::Breakpoints))
        breakpointCount = debuggerPlugin_->GetBreakpointsCount();

    const bool supportsBreakpoints = debuggerPlugin_->SupportsFeature(cbDebuggerFeature::Breakpoints);
    const bool supportsCallstack = debuggerPlugin_->SupportsFeature(cbDebuggerFeature::Callstack);
    const bool supportsThreads = debuggerPlugin_->SupportsFeature(cbDebuggerFeature::Threads);
    const bool supportsWatches = debuggerPlugin_->SupportsFeature(cbDebuggerFeature::Watches);
    const bool supportsValueTooltips = debuggerPlugin_->SupportsFeature(cbDebuggerFeature::ValueTooltips);

    const wxString snapshot = wxString::Format(
        wxS("{\"dataKind\":\"%s\",\"running\":%s,\"stopped\":%s,\"busy\":%s,"
            "\"exitCode\":%d,\"activeFrame\":%d,\"currentFile\":\"%s\","
            "\"currentLine\":%d,\"currentColumn\":0,\"breakpointCount\":%d,"
            "\"supportsBreakpoints\":%s,\"supportsCallstack\":%s,"
            "\"supportsThreads\":%s,\"supportsWatches\":%s,"
            "\"supportsValueTooltips\":%s,\"privateDataAvailable\":false}"),
        JsonEscape(dataKind), running ? wxS("true") : wxS("false"),
        stopped ? wxS("true") : wxS("false"), busy ? wxS("true") : wxS("false"),
        debuggerPlugin_->GetExitCode(), activeFrame, JsonEscape(currentFile), currentLine,
        breakpointCount, supportsBreakpoints ? wxS("true") : wxS("false"),
        supportsCallstack ? wxS("true") : wxS("false"), supportsThreads ? wxS("true") : wxS("false"),
        supportsWatches ? wxS("true") : wxS("false"), supportsValueTooltips ? wxS("true") : wxS("false"));

    CodeBlocksHostEvent event;
    event.kind = CodeBlocksEventKind::DebugSnapshot;
    event.projectPath = project_ ? project_->GetFilename() : wxString();
    event.plugin = wxS("Debugger");
    event.dataKind = dataKind;
    event.snapshotJson = snapshot;
    event.payload = snapshot;
    event.message = wxS("Public Code::Blocks debugger state snapshot");
    PublishSdkEvent(std::move(event));
}

bool CodeBlocksSdkBootstrap::RequestDebugSnapshot(const wxString& dataKind, wxString* error)
{
    if (!started_ || !debuggerPlugin_) {
        Fail(wxS("The Code::Blocks Debugger plugin is not available."), error);
        return false;
    }

    const wxString requested = dataKind.empty() ? wxS("state") : dataKind;
    if (requested != wxS("state")) {
        Fail(wxString::Format(
                 wxS("The matched Code::Blocks SDK does not expose value-owned %s data through its public ABI."),
                 requested),
             error);
        return false;
    }
    PublishDebugSnapshot(requested);
    return true;
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

std::vector<CodeBlocksHostEvent> CodeBlocksSdkBootstrap::DrainEvents()
{
    std::vector<CodeBlocksHostEvent> result;
    result.swap(events_);
    return result;
}

void CodeBlocksSdkBootstrap::Shutdown()
{
    if (!started_) return;
    if (debuggerPlugin_ && debuggerPlugin_->IsRunning()) debuggerPlugin_->Stop();
    ProjectManager* projectManager = Manager::Get()->GetProjectManager();
    if (projectManager) projectManager->CloseAllProjects(true);
    UnbindCompilerOutput();
    UnregisterEventSinks();
    Manager::SetAppShuttingDown(true);
    if (debuggerPlugin_) {
        PluginManager* plugins = Manager::Get()->GetPluginManager();
        if (plugins) plugins->UnloadPlugin(debuggerPlugin_);
        debuggerPlugin_ = nullptr;
    }
    if (compilerPlugin_) {
        PluginManager* plugins = Manager::Get()->GetPluginManager();
        if (plugins) plugins->UnloadPlugin(compilerPlugin_);
        compilerPlugin_ = nullptr;
    }
    Manager::Free();
    project_ = nullptr;
    compilerPlugin_ = nullptr;
    report_.debuggerPluginAttached = false;
    report_.debuggerPluginLoaded = false;
    report_.debuggerControlAvailable = false;
    report_.debuggerSnapshotAvailable = false;
    report_.debuggerPublicStateAvailable = false;
    report_.debuggerPrivateDataAvailable = false;
    RemovePluginStaging();
    started_ = false;
}

} // namespace codium
