#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <vector>

namespace codium {

struct CodeBlocksHostContract final {
    static constexpr int kMajor = 1;
    static constexpr int kMinor = 0;

    static wxString Version();
    static bool Supports(int major, int minor);
};

enum class CodeBlocksEventKind {
    ProjectOpened,
    ProjectClosed,
    BuildStarted,
    BuildFinished,
    CompilerDiagnostic,
    DebugSessionStarted,
    DebugSessionStopped,
    PluginCommand
};

struct CodeBlocksHostEvent final {
    CodeBlocksEventKind kind = CodeBlocksEventKind::ProjectOpened;
    wxString projectPath;
    wxString target;
    wxString plugin;
    wxString command;
    wxString message;
    wxString filePath;
    int line = 0;
    int column = 0;
    int exitCode = 0;
    bool isError = false;
    wxString payload;
};

struct CodeBlocksHostConfiguration final {
    int contractMajor = CodeBlocksHostContract::kMajor;
    int contractMinor = CodeBlocksHostContract::kMinor;
    int sdkMajor = 0;
    int sdkMinor = 0;
    int sdkRelease = 0;
    wxString sdkRoot;
    wxString projectFile;
};

class CodeBlocksEventBus final {
public:
    void Publish(const CodeBlocksHostEvent& event);
    std::vector<CodeBlocksHostEvent> Drain();
    bool Empty() const { return events_.empty(); }

private:
    std::vector<CodeBlocksHostEvent> events_;
};

wxString CodeBlocksEventKindName(CodeBlocksEventKind kind);

} // namespace codium
