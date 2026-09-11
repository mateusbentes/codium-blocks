// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

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
    case CodeBlocksEventKind::ProjectTarget: return wxS("projectTarget");
    case CodeBlocksEventKind::ProjectClosed: return wxS("projectClosed");
    case CodeBlocksEventKind::ProjectActivated: return wxS("projectActivated");
    case CodeBlocksEventKind::ProjectSaved: return wxS("projectSaved");
    case CodeBlocksEventKind::ProjectTargetsChanged: return wxS("projectTargetsChanged");
    case CodeBlocksEventKind::ProjectFileAdded: return wxS("projectFileAdded");
    case CodeBlocksEventKind::ProjectFileRemoved: return wxS("projectFileRemoved");
    case CodeBlocksEventKind::ProjectFileChanged: return wxS("projectFileChanged");
    case CodeBlocksEventKind::ProjectFileRenamed: return wxS("projectFileRenamed");
    case CodeBlocksEventKind::BuildStarted: return wxS("buildStarted");
    case CodeBlocksEventKind::BuildFinished: return wxS("buildFinished");
    case CodeBlocksEventKind::CompilerOutput: return wxS("compilerOutput");
    case CodeBlocksEventKind::CompilerDiagnostic: return wxS("compilerDiagnostic");
    case CodeBlocksEventKind::DebugSessionStarted: return wxS("debugSessionStarted");
    case CodeBlocksEventKind::DebugSessionStopped: return wxS("debugSessionStopped");
    case CodeBlocksEventKind::DebugSessionPaused: return wxS("debugSessionPaused");
    case CodeBlocksEventKind::DebugSessionContinued: return wxS("debugSessionContinued");
    case CodeBlocksEventKind::DebugSessionCursorChanged: return wxS("debugSessionCursorChanged");
    case CodeBlocksEventKind::DebugSessionUpdated: return wxS("debugSessionUpdated");
    case CodeBlocksEventKind::DebugSnapshot: return wxS("debugSnapshot");
    case CodeBlocksEventKind::PluginCommand: return wxS("pluginCommand");
    }
    return wxS("unknown");
}

} // namespace codium

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
