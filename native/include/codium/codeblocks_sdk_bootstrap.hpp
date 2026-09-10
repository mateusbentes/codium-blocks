#pragma once

#include "codium/codeblocks_host.hpp"

#include <wx/string.h>

#include <memory>
#include <vector>

class cbProject;
class cbCompilerPlugin;
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
    bool wxAppReady = false;
    bool resourcesLoaded = false;
    bool compilerPluginLoaded = false;
    bool compilerPluginAttached = false;
    bool projectEnumerationAvailable = false;
    bool eventSinkRegistered = false;
    bool compilerEventsAvailable = false;
    bool compilerOutputAvailable = false;
};

/**
 * Owns the Code::Blocks SDK lifecycle inside the dedicated adapter process.
 *
 * This class is intentionally not linked into the main Codium::Blocks target.
 * The caller must have started a wxApp before constructing it. Only the
 * explicitly selected, matching Code::Blocks compiler plugin is loaded; no
 * plugin directory scan or arbitrary native module loading is performed.
 */
class CodeBlocksSdkBootstrap final {
public:
    explicit CodeBlocksSdkBootstrap(wxFrame* appFrame);
    ~CodeBlocksSdkBootstrap();

    CodeBlocksSdkBootstrap(const CodeBlocksSdkBootstrap&) = delete;
    CodeBlocksSdkBootstrap& operator=(const CodeBlocksSdkBootstrap&) = delete;

    bool Start(const wxString& dataDirectory,
               const wxString& compilerPlugin,
               wxString* error = nullptr);
    cbProject* LoadProject(const wxString& projectFile, wxString* error = nullptr);
    std::vector<CodeBlocksTargetInfo> EnumerateTargets(cbProject* project) const;
    bool BuildProject(const wxString& projectFile,
                      const wxString& target,
                      const wxString& configuration,
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
    void BindCompilerOutput();
    void UnbindCompilerOutput();
    void PublishSdkEvent(CodeBlocksHostEvent event);

    wxFrame* appFrame_ = nullptr;
    cbProject* project_ = nullptr;
    cbCompilerPlugin* compilerPlugin_ = nullptr;
    std::unique_ptr<CodeBlocksCompilerOutputFilter> compilerOutputFilter_;
    CodeBlocksSdkReport report_;
    std::vector<CodeBlocksHostEvent> events_;
    bool eventSinksRegistered_ = false;
    bool started_ = false;
};

} // namespace codium
