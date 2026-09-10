#pragma once

#include "codium/codeblocks_host.hpp"

#include <wx/string.h>

#include <vector>

class cbProject;
class CodeBlocksEvent;
class wxFrame;

namespace codium {

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
    bool projectEnumerationAvailable = false;
    bool eventSinkRegistered = false;
    bool compilerEventsAvailable = false;
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
    std::vector<CodeBlocksHostEvent> DrainEvents();
    void Shutdown();

    bool IsStarted() const { return started_; }
    const CodeBlocksSdkReport& Report() const { return report_; }

private:
    void Fail(const wxString& message, wxString* error);
    void RegisterEventSinks();
    void UnregisterEventSinks();
    void OnSdkEvent(CodeBlocksEvent& event);
    void PublishSdkEvent(CodeBlocksHostEvent event);

    wxFrame* appFrame_ = nullptr;
    cbProject* project_ = nullptr;
    CodeBlocksSdkReport report_;
    std::vector<CodeBlocksHostEvent> events_;
    bool eventSinksRegistered_ = false;
    bool started_ = false;
};

} // namespace codium
