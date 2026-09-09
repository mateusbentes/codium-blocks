#include "codium/project_config.hpp"

#include <wx/filefn.h>
#include <wx/filename.h>

#include <initializer_list>

namespace codium {

namespace {

ProjectTask Task(const wxString& name, const wxString& program,
                 std::initializer_list<wxString> arguments, const wxString& workingDirectory)
{
    ProjectTask task{name, program, {}, workingDirectory};
    for (const auto& argument : arguments) task.arguments.Add(argument);
    return task;
}

} // namespace

bool ProjectConfig::Load(const wxString& workspaceRoot, wxString* error)
{
    toolchains_.Clear();
    tasks_.clear();
    schemes_.clear();
    if (!wxDirExists(workspaceRoot)) {
        if (error) *error = wxString::Format(wxS("Project directory does not exist: %s."), workspaceRoot);
        return false;
    }

    AddBuiltInTasks(workspaceRoot);
    AddBuiltInSchemes();
    LoadCustomTasks(workspaceRoot);
    return true;
}

void ProjectConfig::AddBuiltInTasks(const wxString& workspaceRoot)
{
    if (wxFileExists(workspaceRoot + wxFILE_SEP_PATH + wxS("CMakeLists.txt"))) {
        toolchains_.Add(wxS("CMake"));
        tasks_.push_back(Task(wxS("CMake: Configure"), wxS("cmake"),
                              {wxS("-S"), workspaceRoot, wxS("-B"), workspaceRoot + wxFILE_SEP_PATH + wxS("build")},
                              workspaceRoot));
        tasks_.push_back(Task(wxS("CMake: Build"), wxS("cmake"),
                              {wxS("--build"), workspaceRoot + wxFILE_SEP_PATH + wxS("build")},
                              workspaceRoot));
    }

    if (wxFileExists(workspaceRoot + wxFILE_SEP_PATH + wxS("Makefile")) ||
        wxFileExists(workspaceRoot + wxFILE_SEP_PATH + wxS("makefile"))) {
        toolchains_.Add(wxS("Make"));
        tasks_.push_back(Task(wxS("Make: Build"), wxS("make"), {}, workspaceRoot));
    }

    if (wxFileExists(workspaceRoot + wxFILE_SEP_PATH + wxS("Cargo.toml"))) {
        toolchains_.Add(wxS("Cargo"));
        tasks_.push_back(Task(wxS("Cargo: Build"), wxS("cargo"), {wxS("build")}, workspaceRoot));
        tasks_.push_back(Task(wxS("Cargo: Test"), wxS("cargo"), {wxS("test")}, workspaceRoot));
    }

    if (wxFileExists(workspaceRoot + wxFILE_SEP_PATH + wxS("package.json"))) {
        toolchains_.Add(wxS("npm"));
        tasks_.push_back(Task(wxS("npm: Build"), wxS("npm"), {wxS("run"), wxS("build")}, workspaceRoot));
        tasks_.push_back(Task(wxS("npm: Test"), wxS("npm"), {wxS("test")}, workspaceRoot));
    }
}

void ProjectConfig::AddBuiltInSchemes()
{
    for (const auto& toolchain : toolchains_) {
        const wxString target = toolchain == wxS("CMake") ? wxS("all") :
                                toolchain == wxS("Make") ? wxS("default") :
                                toolchain == wxS("Cargo") ? wxS("workspace") : wxS("package");
        for (const auto& configuration : {wxString(wxS("Debug")), wxString(wxS("Release"))}) {
            schemes_.push_back(ProjectScheme{
                wxString::Format(wxS("%s — %s"), configuration, toolchain),
                configuration,
                target,
                toolchain});
        }
    }
}

void ProjectConfig::LoadCustomTasks(const wxString& workspaceRoot)
{
    // The first native task model is intentionally conservative: built-in tasks
    // come from recognizable project manifests. A future schema can add explicit
    // tasks without changing ProjectTask or TaskRunner.
    (void)workspaceRoot;
}

} // namespace codium
