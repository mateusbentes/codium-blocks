#include "codium/project_config.hpp"

#include <wx/dir.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/xml/xml.h>

#include <algorithm>
#include <initializer_list>

namespace codium {

namespace {

ProjectTask Task(const wxString& name, const wxString& program,
                 std::initializer_list<wxString> arguments, const wxString& workingDirectory)
{
    ProjectTask task{name, program, {}, workingDirectory, wxEmptyString, wxEmptyString};
    for (const auto& argument : arguments) task.arguments.Add(argument);
    return task;
}

wxString AttributeValue(wxXmlNode* node, const wxString& attribute, const wxString& fallback = wxEmptyString)
{
    if (!node) return fallback;
    for (wxXmlAttribute* property = node->GetAttributes(); property; property = property->GetNext()) {
        if (property->GetName() == attribute) return property->GetValue();
    }
    return fallback;
}

wxXmlNode* ChildNode(wxXmlNode* parent, const wxString& name)
{
    if (!parent) return nullptr;
    for (wxXmlNode* child = parent->GetChildren(); child; child = child->GetNext()) {
        if (child->GetName() == name) return child;
    }
    return nullptr;
}

wxString OptionValue(wxXmlNode* parent, const wxString& option, const wxString& fallback = wxEmptyString)
{
    for (wxXmlNode* child = parent ? parent->GetChildren() : nullptr; child; child = child->GetNext()) {
        if (child->GetName() != wxS("Option")) continue;
        const wxString directValue = AttributeValue(child, option);
        if (!directValue.empty()) return directValue;
        const wxString name = AttributeValue(child, wxS("name"));
        if (name == option) return AttributeValue(child, wxS("value"), fallback);
    }
    return fallback;
}

wxString ResolveProjectPath(const wxString& projectDirectory, const wxString& value)
{
    if (value.empty()) return projectDirectory;
    wxFileName path(value);
    if (path.IsAbsolute()) return path.GetFullPath();
    return wxFileName(projectDirectory, value).GetFullPath();
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
    LoadCodeBlocksProjects(workspaceRoot);
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
                toolchain,
                wxEmptyString});
        }
    }
}

void ProjectConfig::LoadCodeBlocksProjects(const wxString& workspaceRoot)
{
    wxDir directory(workspaceRoot);
    if (!directory.IsOpened()) return;

    wxArrayString projectNames;
    wxString filename;
    bool found = directory.GetFirst(&filename, wxS("*.cbp"), wxDIR_FILES);
    while (found) {
        projectNames.Add(filename);
        found = directory.GetNext(&filename);
    }
    projectNames.Sort();
    for (const auto& projectName : projectNames) {
        wxString error;
        if (!LoadCodeBlocksProject(wxFileName(workspaceRoot, projectName).GetFullPath(), &error)) {
            // A malformed optional project must not prevent CMake/Make projects from loading.
            continue;
        }
    }
}

bool ProjectConfig::LoadCodeBlocksProject(const wxString& projectPath, wxString* error)
{
    wxXmlDocument document;
    if (!document.Load(projectPath)) {
        if (error) *error = wxString::Format(wxS("Could not parse Code::Blocks project: %s"), projectPath);
        return false;
    }
    wxXmlNode* root = document.GetRoot();
    if (!root || root->GetName() != wxS("CodeBlocks_project_file")) {
        if (error) *error = wxString::Format(wxS("Invalid Code::Blocks project root: %s"), projectPath);
        return false;
    }
    wxXmlNode* project = ChildNode(root, wxS("Project"));
    wxXmlNode* build = ChildNode(project, wxS("Build"));
    if (!project || !build) {
        if (error) *error = wxString::Format(wxS("Code::Blocks project has no Project/Build section: %s"), projectPath);
        return false;
    }

    const wxString projectDirectory = wxFileName(projectPath).GetPath();
    const wxString projectCompiler = OptionValue(project, wxS("compiler"), wxS("default"));
    const wxString toolchain = wxString::Format(wxS("Code::Blocks (%s)"), projectCompiler);
    if (toolchains_.Index(toolchain) == wxNOT_FOUND) toolchains_.Add(toolchain);

    int targetCount = 0;
    for (wxXmlNode* target = build->GetChildren(); target; target = target->GetNext()) {
        if (target->GetName() != wxS("Target")) continue;
        const wxString title = AttributeValue(target, wxS("title"));
        if (title.empty()) continue;

        const wxString compiler = OptionValue(target, wxS("compiler"), projectCompiler);
        const wxString targetToolchain = wxString::Format(wxS("Code::Blocks (%s)"), compiler);
        if (toolchains_.Index(targetToolchain) == wxNOT_FOUND) toolchains_.Add(targetToolchain);
        wxString workingDirectory = ResolveProjectPath(projectDirectory, OptionValue(target, wxS("working_dir")));
        if (!wxDirExists(workingDirectory)) workingDirectory = projectDirectory;

        ProjectTask task = Task(wxString::Format(wxS("Code::Blocks: Build %s"), title), wxS("codeblocks"),
                                {wxS("--build"), wxString::Format(wxS("--target=%s"), title), projectPath}, workingDirectory);
        task.projectFile = projectPath;
        task.targetName = title;
        tasks_.push_back(task);

        for (const auto& configuration : {wxString(wxS("Debug")), wxString(wxS("Release"))}) {
            schemes_.push_back(ProjectScheme{
                wxString::Format(wxS("%s — Code::Blocks — %s"), configuration, title),
                configuration,
                title,
                targetToolchain,
                projectPath});
        }
        ++targetCount;
    }

    if (targetCount == 0) {
        if (error) *error = wxString::Format(wxS("Code::Blocks project contains no build targets: %s"), projectPath);
        return false;
    }
    return true;
}

void ProjectConfig::LoadCustomTasks(const wxString& workspaceRoot)
{
    // The first native task model is intentionally conservative: built-in tasks
    // come from recognizable project manifests. A future schema can add explicit
    // tasks without changing ProjectTask or TaskRunner.
    (void)workspaceRoot;
}

} // namespace codium
