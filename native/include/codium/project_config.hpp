#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <vector>

namespace codium {

struct ProjectTask final {
    wxString name;
    wxString program;
    wxArrayString arguments;
    wxString workingDirectory;
};

struct ProjectScheme final {
    wxString name;
    wxString configuration;
    wxString target;
    wxString toolchain;
};

class ProjectConfig final {
public:
    bool Load(const wxString& workspaceRoot, wxString* error = nullptr);

    const wxArrayString& Toolchains() const { return toolchains_; }
    const std::vector<ProjectTask>& Tasks() const { return tasks_; }
    const std::vector<ProjectScheme>& Schemes() const { return schemes_; }

private:
    void AddBuiltInTasks(const wxString& workspaceRoot);
    void AddBuiltInSchemes();
    void LoadCustomTasks(const wxString& workspaceRoot);

    wxArrayString toolchains_;
    std::vector<ProjectTask> tasks_;
    std::vector<ProjectScheme> schemes_;
};

} // namespace codium
