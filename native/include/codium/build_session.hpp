// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <cstdint>
#include <vector>

namespace codium {

enum class BuildSessionStatus {
    Running,
    Succeeded,
    Failed,
    Cancelled
};

struct BuildSession final {
    wxString id;
    wxString taskName;
    wxString target;
    wxString configuration;
    wxString toolchain;
    wxString projectFile;
    wxString workingDirectory;
    wxString statusMessage;
    std::int64_t startedAtMillis = 0;
    std::int64_t elapsedMillis = 0;
    int exitCode = 0;
    BuildSessionStatus status = BuildSessionStatus::Running;
    wxArrayString output;

    bool IsRunning() const { return status == BuildSessionStatus::Running; }
    bool Succeeded() const { return status == BuildSessionStatus::Succeeded; }
};

struct BuildSessionSpec final {
    wxString taskName;
    wxString target;
    wxString configuration;
    wxString toolchain;
    wxString projectFile;
    wxString workingDirectory;
};

class BuildSessionStore final {
public:
    bool Load(const wxString& workspaceRoot, wxString* error = nullptr);
    bool Save(wxString* error = nullptr) const;

    wxString Begin(const BuildSessionSpec& specification, wxString* error = nullptr);
    bool AppendOutput(const wxString& id, const wxString& line, wxString* error = nullptr);
    bool Finish(const wxString& id, int exitCode, bool cancelled = false, wxString* error = nullptr);

    const std::vector<BuildSession>& Sessions() const { return sessions_; }
    const BuildSession* Find(const wxString& id) const;
    const BuildSession* Current() const;
    void Clear();

    static wxString StatusName(BuildSessionStatus status);
    static wxString DisplayLabel(const BuildSession& session);

private:
    static std::int64_t NowMillis();
    static BuildSessionStatus ParseStatus(const wxString& value);
    static wxString StoragePath(const wxString& workspaceRoot);

    wxString workspaceRoot_;
    std::vector<BuildSession> sessions_;
    wxString currentId_;
};

} // namespace codium
