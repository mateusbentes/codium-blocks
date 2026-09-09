#include "codium/codeblocks_host.hpp"

#include <utility>

namespace codium {

wxString CodeBlocksHostContract::Version()
{
    return wxString::Format(wxS("%d.%d"), kMajor, kMinor);
}

bool CodeBlocksHostContract::Supports(int major, int minor)
{
    return major == kMajor && minor >= 0 && minor <= kMinor;
}

void CodeBlocksEventBus::Publish(const CodeBlocksHostEvent& event)
{
    events_.push_back(event);
}

std::vector<CodeBlocksHostEvent> CodeBlocksEventBus::Drain()
{
    std::vector<CodeBlocksHostEvent> result;
    result.swap(events_);
    return result;
}

wxString CodeBlocksEventKindName(CodeBlocksEventKind kind)
{
    switch (kind) {
    case CodeBlocksEventKind::ProjectOpened: return wxS("projectOpened");
    case CodeBlocksEventKind::ProjectClosed: return wxS("projectClosed");
    case CodeBlocksEventKind::BuildStarted: return wxS("buildStarted");
    case CodeBlocksEventKind::BuildFinished: return wxS("buildFinished");
    case CodeBlocksEventKind::CompilerDiagnostic: return wxS("compilerDiagnostic");
    case CodeBlocksEventKind::DebugSessionStarted: return wxS("debugSessionStarted");
    case CodeBlocksEventKind::DebugSessionStopped: return wxS("debugSessionStopped");
    case CodeBlocksEventKind::PluginCommand: return wxS("pluginCommand");
    }
    return wxS("unknown");
}

} // namespace codium
