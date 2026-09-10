// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#include "codium/project_config.hpp"

#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/regex.h>
#include <wx/tokenzr.h>
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

void AddUnique(wxArrayString* values, const wxString& value)
{
    if (values && !value.empty() && values->Index(value) == wxNOT_FOUND) values->Add(value);
}

bool ReadText(const wxString& path, wxString* text)
{
    wxFile file;
    return file.Open(path, wxFile::read) && file.ReadAll(text);
}

wxString EscapePreference(const wxString& value)
{
    wxString result;
    for (const wxChar character : value) {
        if (character == wxChar('\\')) result += wxS("\\\\");
        else if (character == wxChar('\t')) result += wxS("\\t");
        else if (character == wxChar('\n')) result += wxS("\\n");
        else result += character;
    }
    return result;
}

wxString UnescapePreference(const wxString& value)
{
    wxString result;
    bool escaped = false;
    for (const wxChar character : value) {
        if (!escaped && character == wxChar('\\')) {
            escaped = true;
            continue;
        }
        if (escaped) {
            result += character == wxChar('t') ? wxChar('\t') :
                      character == wxChar('n') ? wxChar('\n') : character;
            escaped = false;
        } else {
            result += character;
        }
    }
    return result;
}

wxArrayString TabFields(const wxString& line)
{
    wxArrayString result;
    wxString field;
    for (const wxChar character : line) {
        if (character == wxChar('\t')) {
            result.Add(UnescapePreference(field));
            field.clear();
        } else {
            field += character;
        }
    }
    result.Add(UnescapePreference(field));
    return result;
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
    wxFileName joined(projectDirectory + wxFILE_SEP_PATH + value);
    joined.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    return joined.GetFullPath();
}

wxString FirstToken(const wxString& value)
{
    wxString token = value;
    token.Trim(true).Trim(false);
    int end = 0;
    while (end < static_cast<int>(token.length()) &&
           token[end] != wxChar(' ') && token[end] != wxChar('\t') &&
           token[end] != wxChar(')') && token[end] != wxChar(',')) {
        ++end;
    }
    if (end < static_cast<int>(token.length())) token = token.Left(end);
    return token;
}

} // namespace

bool ProjectConfig::Load(const wxString& workspaceRoot, wxString* error)
{
    toolchains_.Clear();
    targets_.clear();
    tasks_.clear();
    schemes_.clear();
    if (!wxDirExists(workspaceRoot)) {
        if (error) *error = wxString::Format(wxS("Project directory does not exist: %s."), workspaceRoot);
        return false;
    }

    AddBuiltInTasks(workspaceRoot);
    LoadCodeBlocksProjects(workspaceRoot);
    LoadCustomTasks(workspaceRoot);
    AddBuiltInSchemes();
    return true;
}

void ProjectConfig::AddBuiltInTasks(const wxString& workspaceRoot)
{
    if (wxFileExists(workspaceRoot + wxFILE_SEP_PATH + wxS("CMakeLists.txt"))) {
        AddUnique(&toolchains_, wxS("CMake"));
        tasks_.push_back(Task(wxS("CMake: Configure"), wxS("cmake"),
                              {wxS("-S"), workspaceRoot, wxS("-B"), workspaceRoot + wxFILE_SEP_PATH + wxS("build")},
                              workspaceRoot));
        tasks_.push_back(Task(wxS("CMake: Build"), wxS("cmake"),
                              {wxS("--build"), workspaceRoot + wxFILE_SEP_PATH + wxS("build")}, workspaceRoot));
        LoadCMakeTargets(workspaceRoot);
    }

    if (wxFileExists(workspaceRoot + wxFILE_SEP_PATH + wxS("Makefile")) ||
        wxFileExists(workspaceRoot + wxFILE_SEP_PATH + wxS("makefile"))) {
        AddUnique(&toolchains_, wxS("Make"));
        tasks_.push_back(Task(wxS("Make: Build"), wxS("make"), {}, workspaceRoot));
        LoadMakeTargets(workspaceRoot);
    }

    if (wxFileExists(workspaceRoot + wxFILE_SEP_PATH + wxS("Cargo.toml"))) {
        AddUnique(&toolchains_, wxS("Cargo"));
        tasks_.push_back(Task(wxS("Cargo: Build"), wxS("cargo"), {wxS("build")}, workspaceRoot));
        tasks_.push_back(Task(wxS("Cargo: Test"), wxS("cargo"), {wxS("test")}, workspaceRoot));
        LoadCargoTargets(workspaceRoot);
    }

    if (wxFileExists(workspaceRoot + wxFILE_SEP_PATH + wxS("package.json"))) {
        AddUnique(&toolchains_, wxS("npm"));
        tasks_.push_back(Task(wxS("npm: Build"), wxS("npm"), {wxS("run"), wxS("build")}, workspaceRoot));
        tasks_.push_back(Task(wxS("npm: Test"), wxS("npm"), {wxS("test")}, workspaceRoot));
        LoadNpmTargets(workspaceRoot);
    }
}

void ProjectConfig::AddTarget(ProjectTarget target)
{
    if (target.name.empty()) return;
    if (target.id.empty()) target.id = target.toolchain + wxS(":") + target.name + wxS(":") + target.projectFile;
    for (const auto& existing : targets_) {
        if (existing.id == target.id) return;
    }
    targets_.push_back(std::move(target));
}

void ProjectConfig::AddSchemeForTarget(const ProjectTarget& target, const wxString& configuration)
{
    const wxString name = wxString::Format(wxS("%s — %s — %s"), configuration, target.toolchain, target.name);
    for (const auto& scheme : schemes_) {
        if (scheme.name == name) return;
    }
    schemes_.push_back(ProjectScheme{name, configuration, target.name, target.toolchain, target.projectFile});
}

void ProjectConfig::AddBuildTaskForTarget(const ProjectTarget& target, const wxString& program,
                                          const wxArrayString& arguments)
{
    if (target.buildTaskName.empty()) return;
    for (const auto& task : tasks_) {
        if (task.name == target.buildTaskName && task.targetName == target.name) return;
    }
    ProjectTask task{target.buildTaskName, program, arguments, target.workingDirectory,
                     target.projectFile, target.name};
    tasks_.push_back(task);
}

void ProjectConfig::LoadCMakeTargets(const wxString& workspaceRoot)
{
    wxString text;
    if (!ReadText(workspaceRoot + wxFILE_SEP_PATH + wxS("CMakeLists.txt"), &text)) return;
    const wxArrayString lines = wxSplit(text, wxChar('\n'));
    for (wxString line : lines) {
        line.Trim(true).Trim(false);
        wxString lower = line.Lower();
        ProjectTargetKind kind;
        if (lower.StartsWith(wxS("add_executable("))) kind = ProjectTargetKind::Executable;
        else if (lower.StartsWith(wxS("add_library("))) kind = ProjectTargetKind::Library;
        else continue;
        const int open = line.Find(wxChar('('));
        if (open == wxNOT_FOUND) continue;
        wxString name = FirstToken(line.Mid(open + 1));
        if (name.empty() || name.StartsWith(wxS("${"))) continue;
        ProjectTarget target;
        target.id = wxS("CMake:") + name;
        target.name = name;
        target.toolchain = wxS("CMake");
        target.workingDirectory = workspaceRoot;
        target.buildTaskName = wxS("CMake: Build");
        target.kind = kind;
        target.supportsRun = kind == ProjectTargetKind::Executable;
        target.supportsDebug = target.supportsRun;
        target.runProgram = workspaceRoot + wxFILE_SEP_PATH + wxS("build") + wxFILE_SEP_PATH + name;
        target.outputPath = target.runProgram;
        AddTarget(target);
        wxArrayString arguments;
        arguments.Add(wxS("--build"));
        arguments.Add(workspaceRoot + wxFILE_SEP_PATH + wxS("build"));
        arguments.Add(wxS("--target"));
        arguments.Add(name);
        AddBuildTaskForTarget(target, wxS("cmake"), arguments);
    }
    if (targets_.empty() || std::none_of(targets_.begin(), targets_.end(), [](const ProjectTarget& target) {
            return target.toolchain == wxS("CMake");
        })) {
        AddTarget(ProjectTarget{wxS("CMake:all"), wxS("all"), wxS("CMake"), wxEmptyString,
                                 workspaceRoot, wxEmptyString, wxS("CMake: Build"), wxEmptyString, {},
                                 ProjectTargetKind::Aggregate, true, false, false});
    }
}

void ProjectConfig::LoadMakeTargets(const wxString& workspaceRoot)
{
    const wxString filename = wxFileExists(workspaceRoot + wxFILE_SEP_PATH + wxS("Makefile"))
        ? workspaceRoot + wxFILE_SEP_PATH + wxS("Makefile")
        : workspaceRoot + wxFILE_SEP_PATH + wxS("makefile");
    wxString text;
    if (!ReadText(filename, &text)) return;
    const wxArrayString lines = wxSplit(text, wxChar('\n'));
    bool found = false;
    for (wxString line : lines) {
        if (line.empty() || line[0] == wxChar(' ') || line[0] == wxChar('\t')) continue;
        const int colon = line.Find(wxChar(':'));
        if (colon <= 0) continue;
        wxString name = line.Left(colon);
        name.Trim(true).Trim(false);
        if (name.empty() || name.Contains(wxS("%")) || name.Contains(wxS("=")) || name == wxS(".PHONY")) continue;
        ProjectTarget target;
        target.id = wxS("Make:") + name;
        target.name = name;
        target.toolchain = wxS("Make");
        target.workingDirectory = workspaceRoot;
        target.buildTaskName = wxString::Format(wxS("Make: Build %s"), name);
        target.kind = name == wxS("all") || name == wxS("default") ? ProjectTargetKind::Aggregate : ProjectTargetKind::Executable;
        target.supportsRun = false;
        target.supportsDebug = false;
        AddTarget(target);
        wxArrayString arguments;
        arguments.Add(name);
        AddBuildTaskForTarget(target, wxS("make"), arguments);
        found = true;
    }
    if (!found) {
        ProjectTarget target{wxS("Make:default"), wxS("default"), wxS("Make"), wxEmptyString,
                            workspaceRoot, wxEmptyString, wxS("Make: Build"), wxEmptyString, {},
                            ProjectTargetKind::Aggregate, true, false, false};
        AddTarget(target);
    }
}

void ProjectConfig::LoadCargoTargets(const wxString& workspaceRoot)
{
    wxString text;
    if (!ReadText(workspaceRoot + wxFILE_SEP_PATH + wxS("Cargo.toml"), &text)) return;
    bool inBin = false;
    bool found = false;
    wxRegEx nameExpression(wxS("^[[:space:]]*name[[:space:]]*=[[:space:]]*[\\\"]([^\\\"]+)[\\\"]"));
    for (wxString line : wxSplit(text, wxChar('\n'))) {
        line.Trim(true).Trim(false);
        if (line.StartsWith(wxS("[[bin]]"))) {
            inBin = true;
            continue;
        }
        if (line.StartsWith(wxS("[")) && !line.StartsWith(wxS("[[bin]]"))) inBin = false;
        if (!inBin || !nameExpression.IsValid() || !nameExpression.Matches(line)) continue;
        const wxString name = nameExpression.GetMatch(line, 1);
        ProjectTarget target{wxS("Cargo:") + name, name, wxS("Cargo"), wxEmptyString,
                             workspaceRoot, wxEmptyString, wxString::Format(wxS("Cargo: Build %s"), name),
                             workspaceRoot + wxFILE_SEP_PATH + wxS("target") + wxFILE_SEP_PATH + wxS("debug") + wxFILE_SEP_PATH + name,
                             {}, ProjectTargetKind::Executable, true, true, true};
        AddTarget(target);
        wxArrayString arguments;
        arguments.Add(wxS("build"));
        arguments.Add(wxS("--bin"));
        arguments.Add(name);
        AddBuildTaskForTarget(target, wxS("cargo"), arguments);
        found = true;
        inBin = false;
    }
    if (!found) {
        AddTarget(ProjectTarget{wxS("Cargo:workspace"), wxS("workspace"), wxS("Cargo"), wxEmptyString,
                                workspaceRoot, wxEmptyString, wxS("Cargo: Build"), wxEmptyString, {},
                                ProjectTargetKind::Aggregate, true, false, false});
    }
}

void ProjectConfig::LoadNpmTargets(const wxString& workspaceRoot)
{
    wxString text;
    if (!ReadText(workspaceRoot + wxFILE_SEP_PATH + wxS("package.json"), &text)) return;
    bool inScripts = false;
    wxRegEx scriptExpression(wxS("^[[:space:]]*[\\\"]([^\\\"]+)[\\\"][[:space:]]*:[[:space:]]*"));
    for (wxString line : wxSplit(text, wxChar('\n'))) {
        if (line.Find(wxS("\"scripts\"")) != wxNOT_FOUND) {
            inScripts = true;
            continue;
        }
        if (!inScripts) continue;
        if (line.Find(wxChar('}')) != wxNOT_FOUND) {
            inScripts = false;
            continue;
        }
        if (!scriptExpression.IsValid() || !scriptExpression.Matches(line)) continue;
        const wxString name = scriptExpression.GetMatch(line, 1);
        if (name.empty()) continue;
        ProjectTarget target{wxS("npm:") + name, name, wxS("npm"), wxEmptyString,
                             workspaceRoot, wxEmptyString, wxString::Format(wxS("npm: Run %s"), name),
                             wxEmptyString, {}, ProjectTargetKind::Script, true, false, false};
        AddTarget(target);
        wxArrayString arguments;
        arguments.Add(wxS("run"));
        arguments.Add(name);
        AddBuildTaskForTarget(target, wxS("npm"), arguments);
    }
    if (std::none_of(targets_.begin(), targets_.end(), [](const ProjectTarget& target) {
            return target.toolchain == wxS("npm");
        })) {
        AddTarget(ProjectTarget{wxS("npm:package"), wxS("package"), wxS("npm"), wxEmptyString,
                                workspaceRoot, wxEmptyString, wxS("npm: Build"), wxEmptyString, {},
                                ProjectTargetKind::Script, true, false, false});
    }
}

void ProjectConfig::AddBuiltInSchemes()
{
    for (const auto& target : targets_) {
        for (const auto& configuration : {wxString(wxS("Debug")), wxString(wxS("Release"))}) {
            AddSchemeForTarget(target, configuration);
        }
    }
    if (schemes_.empty()) {
        for (const auto& toolchain : toolchains_) {
            const wxString target = toolchain == wxS("CMake") ? wxS("all") :
                                    toolchain == wxS("Make") ? wxS("default") :
                                    toolchain == wxS("Cargo") ? wxS("workspace") : wxS("package");
            schemes_.push_back(ProjectScheme{wxString::Format(wxS("Debug — %s"), toolchain), wxS("Debug"), target,
                                              toolchain, wxEmptyString});
            schemes_.push_back(ProjectScheme{wxString::Format(wxS("Release — %s"), toolchain), wxS("Release"), target,
                                              toolchain, wxEmptyString});
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
        LoadCodeBlocksProject(wxFileName(workspaceRoot, projectName).GetFullPath(), &error);
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
    AddUnique(&toolchains_, toolchain);

    int targetCount = 0;
    for (wxXmlNode* targetNode = build->GetChildren(); targetNode; targetNode = targetNode->GetNext()) {
        if (targetNode->GetName() != wxS("Target")) continue;
        const wxString title = AttributeValue(targetNode, wxS("title"));
        if (title.empty()) continue;
        const wxString compiler = OptionValue(targetNode, wxS("compiler"), projectCompiler);
        const wxString targetToolchain = wxString::Format(wxS("Code::Blocks (%s)"), compiler);
        AddUnique(&toolchains_, targetToolchain);
        wxString workingDirectory = ResolveProjectPath(projectDirectory, OptionValue(targetNode, wxS("working_dir")));
        if (!wxDirExists(workingDirectory)) workingDirectory = projectDirectory;
        const wxString output = ResolveProjectPath(workingDirectory, OptionValue(targetNode, wxS("output")));
        ProjectTarget imported{projectPath + wxS(":") + title, title, targetToolchain, projectPath,
                              workingDirectory, output, wxString::Format(wxS("Code::Blocks: Build %s"), title),
                              output, {}, ProjectTargetKind::CodeBlocks, true, !output.empty(), !output.empty()};
        AddTarget(imported);

        ProjectTask task = Task(imported.buildTaskName, wxS("codeblocks"),
                                {wxS("--build"), wxString::Format(wxS("--target=%s"), title), projectPath}, workingDirectory);
        task.projectFile = projectPath;
        task.targetName = title;
        tasks_.push_back(task);
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
    const wxString path = workspaceRoot + wxFILE_SEP_PATH + wxS(".codium-blocks") + wxFILE_SEP_PATH + wxS("tasks.tsv");
    if (!wxFileExists(path)) return;
    wxString text;
    if (!ReadText(path, &text)) return;
    for (wxString line : wxSplit(text, wxChar('\n'))) {
        if (line.empty() || line.StartsWith(wxS("#"))) continue;
        const wxArrayString fields = TabFields(line);
        if (fields.size() < 4) continue;
        ProjectTask task{fields[0], fields[1], wxSplit(fields[2], wxChar('\x1f')), fields[3],
                         fields.size() > 4 ? fields[4] : wxString(wxEmptyString),
                         fields.size() > 5 ? fields[5] : wxString(wxEmptyString)};
        tasks_.push_back(task);
        if (toolchains_.Index(wxS("Custom")) == wxNOT_FOUND) toolchains_.Add(wxS("Custom"));
    }
}

bool ProjectConfig::LoadPreferences(const wxString& workspaceRoot, ProjectPreferences* preferences,
                                    wxString* error) const
{
    if (!preferences) return false;
    *preferences = ProjectPreferences();
    const wxString path = workspaceRoot + wxFILE_SEP_PATH + wxS(".codium-blocks") + wxFILE_SEP_PATH + wxS("project-preferences.tsv");
    if (!wxFileExists(path)) return true;
    wxString text;
    if (!ReadText(path, &text)) {
        if (error) *error = wxString::Format(wxS("Could not read project preferences: %s."), path);
        return false;
    }
    for (wxString line : wxSplit(text, wxChar('\n'))) {
        if (line.StartsWith(wxS("selection\t"))) {
            const wxArrayString fields = TabFields(line.Mid(10));
            if (fields.size() >= 4) {
                preferences->schemeName = fields[0];
                preferences->configuration = fields[1];
                preferences->target = fields[2];
                preferences->toolchain = fields[3];
            }
            break;
        }
    }
    return true;
}

bool ProjectConfig::SavePreferences(const wxString& workspaceRoot, const ProjectPreferences& preferences,
                                    wxString* error) const
{
    const wxString directory = workspaceRoot + wxFILE_SEP_PATH + wxS(".codium-blocks");
    wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const wxString path = directory + wxFILE_SEP_PATH + wxS("project-preferences.tsv");
    const wxString temporary = path + wxS(".tmp");
    wxFile file;
    if (!file.Open(temporary, wxFile::write)) {
        if (error) *error = wxString::Format(wxS("Could not write project preferences: %s."), temporary);
        return false;
    }
    const wxString content = wxS("version=1\nselection\t") + EscapePreference(preferences.schemeName) + wxS("\t") +
                             EscapePreference(preferences.configuration) + wxS("\t") + EscapePreference(preferences.target) +
                             wxS("\t") + EscapePreference(preferences.toolchain) + wxS("\n");
    const wxScopedCharBuffer bytes = content.utf8_str();
    if (file.Write(bytes.data(), bytes.length()) != bytes.length() || !file.Close() || !wxRenameFile(temporary, path, true)) {
        wxRemoveFile(temporary);
        if (error) *error = wxString::Format(wxS("Could not commit project preferences: %s."), path);
        return false;
    }
    return true;
}

} // namespace codium
