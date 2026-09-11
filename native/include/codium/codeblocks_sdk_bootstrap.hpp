// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include "codium/codeblocks_host.hpp"
#include "codium/codeblocks_debugger_provider.hpp"

#include <wx/dynlib.h>
#include <wx/string.h>

#include <memory>
#include <vector>

class cbProject;
class cbCompilerPlugin;
class cbDebuggerPlugin;
class CodeBlocksEvent;
class wxFrame;

namespace codium {

class CodeBlocksCompilerOutputFilter;

struct CodeBlocksTargetInfo final {
    wxString title;
    wxString compilerId;
    wxString outputPath;
    wxString workingDirectory;
};

struct CodeBlocksSdkReport final {
    int sdkMajor = 0;
    int sdkMinor = 0;
    int sdkRelease = 0;
    wxString sdkIdentity;
    wxString dataDirectory;
    wxString compilerPlugin;
    wxString debuggerPlugin;
    wxString pluginDirectory;
    wxString pluginPolicy;
    bool wxAppReady = false;
    bool resourcesLoaded = false;
    bool compilerPluginLoaded = false;
    bool compilerPluginAttached = false;
    bool projectEnumerationAvailable = false;
    bool eventSinkRegistered = false;
    bool compilerEventsAvailable = false;
    bool compilerOutputAvailable = false;
    bool debuggerPluginLoaded = false;
    bool debuggerPluginAttached = false;
    bool debuggerEventsAvailable = false;
    bool debuggerControlAvailable = false;
    bool debuggerSnapshotAvailable = false;
    bool debuggerPublicStateAvailable = false;
    bool debuggerPrivateDataAvailable = false;
    bool debuggerPrivateProviderLoaded = false;
    bool debuggerPrivateProviderAttached = false;
    wxString debuggerProviderIdentity;
    wxString debuggerProviderSourceRevision;
    wxString debuggerProviderAbiIdentity;
    bool workspaceTrusted = false;
};

/**
 * Owns the Code::Blocks SDK lifecycle inside the dedicated adapter process.
 *
 * This class is intentionally not linked into the main Codium::Blocks target.
 * The caller must have started a wxApp before constructing it. Only explicitly
 * selected, matching Code::Blocks Compiler and optional Debugger plugins are
 * loaded; no complete plugin-directory scan or arbitrary native module loading
 * is performed.
 */
class CodeBlocksSdkBootstrap final {
public:
    explicit CodeBlocksSdkBootstrap(wxFrame* appFrame);
    ~CodeBlocksSdkBootstrap();

    CodeBlocksSdkBootstrap(const CodeBlocksSdkBootstrap&) = delete;
    CodeBlocksSdkBootstrap& operator=(const CodeBlocksSdkBootstrap&) = delete;

    bool Start(const wxString& dataDirectory,
               const wxString& compilerPlugin,
               wxString* error = nullptr,
               const wxString& debuggerPlugin = wxString(),
               const wxString& debuggerProvider = wxString(),
               const wxString& pluginDirectory = wxString(),
               bool workspaceTrusted = true);
    cbProject* LoadProject(const wxString& projectFile, wxString* error = nullptr);
    std::vector<CodeBlocksTargetInfo> EnumerateTargets(cbProject* project) const;
    bool BuildProject(const wxString& projectFile,
                      const wxString& target,
                      const wxString& configuration,
                      wxString* error = nullptr);
    bool StartDebug(const wxString& projectFile,
                    const wxString& target,
                    bool breakOnEntry,
                    wxString* error = nullptr);
    bool ContinueDebug(wxString* error = nullptr);
    bool PauseDebug(wxString* error = nullptr);
    bool StopDebug(wxString* error = nullptr);
    bool RequestDebugSnapshot(const wxString& dataKind,
                              const wxString& expression = wxString(),
                              wxString* error = nullptr);
    std::vector<CodeBlocksHostEvent> DrainEvents();
    void Shutdown();

    bool IsStarted() const { return started_; }
    const CodeBlocksSdkReport& Report() const { return report_; }

private:
    friend class CodeBlocksCompilerOutputFilter;

    void Fail(const wxString& message, wxString* error);
    void RegisterEventSinks();
    void UnregisterEventSinks();
    void OnSdkEvent(CodeBlocksEvent& event);
    void OnCompilerOutput(CodeBlocksEvent& event);
    void OnCompilerError(CodeBlocksEvent& event);
    void PublishDebugSnapshot(const wxString& dataKind);
    void BindCompilerOutput();
    void UnbindCompilerOutput();
    void RemovePluginStaging();
    bool LoadDebuggerProvider(const wxString& providerPath, wxString* error);
    void UnloadDebuggerProvider();
    void PublishSdkEvent(CodeBlocksHostEvent event);

    wxFrame* appFrame_ = nullptr;
    cbProject* project_ = nullptr;
    cbCompilerPlugin* compilerPlugin_ = nullptr;
    cbDebuggerPlugin* debuggerPlugin_ = nullptr;
    std::unique_ptr<wxDynamicLibrary> debuggerProviderLibrary_;
    const CodeBlocksDebuggerProviderApi* debuggerProviderApi_ = nullptr;
    wxString pluginStagingDirectory_;
    std::unique_ptr<CodeBlocksCompilerOutputFilter> compilerOutputFilter_;
    CodeBlocksSdkReport report_;
    std::vector<CodeBlocksHostEvent> events_;
    bool eventSinksRegistered_ = false;
    bool managerInitialized_ = false;
    bool started_ = false;
};

} // namespace codium
