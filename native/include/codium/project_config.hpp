// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <vector>

namespace codium {

enum class ProjectTargetKind {
    Aggregate,
    Executable,
    Library,
    Script,
    CodeBlocks
};

enum class ProjectTaskKind {
    Generic,
    Configure,
    Build,
    Run,
    Test
};

struct ProjectTarget final {
    wxString id;
    wxString name;
    wxString toolchain;
    wxString projectFile;
    wxString workingDirectory;
    wxString outputPath;
    wxString buildTaskName;
    wxString runProgram;
    wxArrayString runArguments;
    ProjectTargetKind kind = ProjectTargetKind::Aggregate;
    bool supportsBuild = true;
    bool supportsRun = false;
    bool supportsDebug = false;
    wxString buildDirectory;
    wxArrayString artifactCandidates;
};

struct ProjectTask final {
    wxString name;
    wxString program;
    wxArrayString arguments;
    wxString workingDirectory;
    wxString projectFile;
    wxString targetName;
    ProjectTaskKind kind = ProjectTaskKind::Generic;
    wxString toolchain;
};

struct ProjectScheme final {
    wxString name;
    wxString configuration;
    wxString target;
    wxString toolchain;
    wxString projectFile;
    wxString buildTaskName;
    wxString runTaskName;
    wxString artifactPath;
};

struct ProjectPreferences final {
    wxString schemeName;
    wxString configuration;
    wxString target;
    wxString toolchain;
};

class ProjectConfig final {
public:
    bool Load(const wxString& workspaceRoot, wxString* error = nullptr);
    bool LoadPreferences(const wxString& workspaceRoot, ProjectPreferences* preferences,
                         wxString* error = nullptr) const;
    bool SavePreferences(const wxString& workspaceRoot, const ProjectPreferences& preferences,
                         wxString* error = nullptr) const;

    static wxArrayString ArtifactCandidates(const ProjectTarget& target, const wxString& configuration,
                                            const wxString& overridePath = wxEmptyString);
    static wxString DiscoverArtifact(const ProjectTarget& target, const wxString& configuration,
                                     const wxString& overridePath = wxEmptyString);

    const wxArrayString& Toolchains() const { return toolchains_; }
    const std::vector<ProjectTarget>& Targets() const { return targets_; }
    const std::vector<ProjectTask>& Tasks() const { return tasks_; }
    const std::vector<ProjectScheme>& Schemes() const { return schemes_; }

private:
    void AddBuiltInTasks(const wxString& workspaceRoot);
    void AddBuiltInSchemes();
    void LoadCustomTasks(const wxString& workspaceRoot);
    void LoadCustomSchemes(const wxString& workspaceRoot);
    void LoadCMakeTargets(const wxString& workspaceRoot, const wxString& buildDirectory);
    void LoadNinjaTargets(const wxString& workspaceRoot, const wxString& buildDirectory);
    void LoadMakeTargets(const wxString& workspaceRoot);
    void LoadCargoTargets(const wxString& workspaceRoot);
    void LoadNpmTargets(const wxString& workspaceRoot);
    void LoadCodeBlocksProjects(const wxString& workspaceRoot);
    bool LoadCodeBlocksProject(const wxString& projectPath, wxString* error);
    void AddTarget(ProjectTarget target);
    void AddSchemeForTarget(const ProjectTarget& target, const wxString& configuration);
    void AddBuildTaskForTarget(const ProjectTarget& target, const wxString& program,
                               const wxArrayString& arguments);

    wxArrayString toolchains_;
    std::vector<ProjectTarget> targets_;
    std::vector<ProjectTask> tasks_;
    std::vector<ProjectScheme> schemes_;
};

} // namespace codium
