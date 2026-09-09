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

class ProjectConfig final {
public:
    bool Load(const wxString& workspaceRoot, wxString* error = nullptr);

    const wxArrayString& Toolchains() const { return toolchains_; }
    const std::vector<ProjectTask>& Tasks() const { return tasks_; }

private:
    void AddBuiltInTasks(const wxString& workspaceRoot);
    void LoadCustomTasks(const wxString& workspaceRoot);

    wxArrayString toolchains_;
    std::vector<ProjectTask> tasks_;
};

} // namespace codium
