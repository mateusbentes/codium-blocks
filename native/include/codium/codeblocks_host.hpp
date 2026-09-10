#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <vector>

namespace codium {

struct CodeBlocksHostContract final {
    static constexpr int kMajor = 1;
    static constexpr int kMinor = 2;

    static wxString Version();
    static bool Supports(int major, int minor);
};

enum class CodeBlocksEventKind {
    ProjectOpened,
    ProjectTarget,
    ProjectClosed,
    ProjectActivated,
    ProjectSaved,
    ProjectTargetsChanged,
    ProjectFileAdded,
    ProjectFileRemoved,
    ProjectFileChanged,
    ProjectFileRenamed,
    BuildStarted,
    BuildFinished,
    CompilerOutput,
    CompilerDiagnostic,
    DebugSessionStarted,
    DebugSessionStopped,
    DebugSessionPaused,
    DebugSessionContinued,
    DebugSessionCursorChanged,
    DebugSessionUpdated,
    DebugSnapshot,
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
    wxString compilerId;
    wxString outputPath;
    wxString workingDirectory;
    int line = 0;
    int column = 0;
    int exitCode = 0;
    bool isError = false;
    wxString payload;
    wxString oldFilePath;
    // Additive Phase E.1 fields. Existing 1.0/1.1 aggregate initializers
    // remain source-compatible because these fields are trailing and optional.
    wxString dataKind;
    wxString snapshotJson;
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

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
// The contract remains intentionally value-owned across the process boundary.
