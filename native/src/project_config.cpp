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
    ProjectTask task{name, program, {}, workingDirectory, wxEmptyString, wxEmptyString,
                     ProjectTaskKind::Generic, wxEmptyString};
    for (const auto& argument : arguments) task.arguments.Add(argument);
    task.toolchain = program;
    if (name.Contains(wxS("Configure"))) task.kind = ProjectTaskKind::Configure;
    else if (name.Contains(wxS("Build"))) task.kind = ProjectTaskKind::Build;
    else if (name.Contains(wxS("Test"))) task.kind = ProjectTaskKind::Test;
    else if (name.StartsWith(wxS("Run")) || name.Contains(wxS(": Run"))) task.kind = ProjectTaskKind::Run;
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

wxString TrimJsonString(wxString value)
{
    value.Trim(true).Trim(false);
    if (value.StartsWith(wxS("\"")) && value.EndsWith(wxS("\"")) && value.length() >= 2) {
        value = value.Mid(1, value.length() - 2);
    }
    value.Replace(wxS("\\\""), wxS("\""));
    return value;
}

wxString ReplacePathTokens(wxString value, const wxString& workspaceRoot, const wxString& configuration)
{
    value.Replace(wxS("${sourceDir}"), workspaceRoot);
    value.Replace(wxS("${workspaceFolder}"), workspaceRoot);
    value.Replace(wxS("${configuration}"), configuration);
    value.Replace(wxS("${config}"), configuration);
    value.Replace(wxS("${presetName}"), configuration);
    value.Replace(wxS("$<CONFIG>"), configuration);
    return value;
}

void AddUniqueTargetPath(wxArrayString* paths, const wxString& value,
                         const wxString& workingDirectory, const wxString& configuration)
{
    if (!paths || value.empty()) return;
    wxString resolved = value;
    resolved.Replace(wxS("${configuration}"), configuration);
    resolved.Replace(wxS("${config}"), configuration);
    resolved.Replace(wxS("$<CONFIG>"), configuration);
    wxFileName path(resolved);
    if (!path.IsAbsolute() && !workingDirectory.empty()) {
        path = wxFileName(workingDirectory, resolved);
    }
    const wxString fullPath = path.GetFullPath();
    if (paths->Index(fullPath) == wxNOT_FOUND) paths->Add(fullPath);
#if defined(__WXMSW__)
    if (!fullPath.EndsWith(wxS(".exe")) && wxFileExists(fullPath + wxS(".exe")) &&
        paths->Index(fullPath + wxS(".exe")) == wxNOT_FOUND) {
        paths->Add(fullPath + wxS(".exe"));
    }
#endif
}

wxString PresetBinaryDirectory(const wxString& workspaceRoot)
{
    for (const auto& filename : {wxString(wxS("CMakePresets.json")), wxString(wxS("CMakeUserPresets.json"))}) {
        wxString text;
        if (!ReadText(workspaceRoot + wxFILE_SEP_PATH + filename, &text)) continue;
        wxRegEx binaryDirectory(wxS("\\\"binaryDir\\\"[[:space:]]*:[[:space:]]*\\\"([^\\\"]+)\\\""));
        if (!binaryDirectory.IsValid() || !binaryDirectory.Matches(text)) continue;
        const wxString value = TrimJsonString(binaryDirectory.GetMatch(text, 1));
        if (!value.empty()) return ReplacePathTokens(value, workspaceRoot, wxS("Debug"));
    }
    return wxEmptyString;
}

wxString FirstPresetName(const wxString& workspaceRoot)
{
    const wxString paths[] = {
        workspaceRoot + wxFILE_SEP_PATH + wxS("CMakePresets.json"),
        workspaceRoot + wxFILE_SEP_PATH + wxS("CMakeUserPresets.json")
    };
    for (const auto& path : paths) {
        wxString text;
        if (!ReadText(path, &text)) continue;
        wxRegEx nameExpression(wxS("\\\"name\\\"[[:space:]]*:[[:space:]]*\\\"([^\\\"]+)\\\""));
        if (nameExpression.IsValid() && nameExpression.Matches(text)) {
            return nameExpression.GetMatch(text, 1);
        }
    }
    return wxEmptyString;
}

wxString ExistingGeneratorDirectory(const wxString& workspaceRoot)
{
    const wxString candidates[] = {
        workspaceRoot + wxFILE_SEP_PATH + wxS("build"),
        workspaceRoot + wxFILE_SEP_PATH + wxS("out"),
        workspaceRoot
    };
    for (const auto& directory : candidates) {
        if (wxFileExists(directory + wxFILE_SEP_PATH + wxS("build.ninja")) ||
            wxFileExists(directory + wxFILE_SEP_PATH + wxS("Makefile")) ||
            wxFileExists(directory + wxFILE_SEP_PATH + wxS("makefile"))) {
            return directory;
        }
    }
    return wxEmptyString;
}

ProjectTaskKind TaskKindFromValue(const wxString& value)
{
    const wxString lower = value.Lower();
    if (lower == wxS("configure")) return ProjectTaskKind::Configure;
    if (lower == wxS("build")) return ProjectTaskKind::Build;
    if (lower == wxS("run")) return ProjectTaskKind::Run;
    if (lower == wxS("test")) return ProjectTaskKind::Test;
    return ProjectTaskKind::Generic;
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
    LoadCustomSchemes(workspaceRoot);
    AddBuiltInSchemes();
    return true;
}

void ProjectConfig::AddBuiltInTasks(const wxString& workspaceRoot)
{
    if (wxFileExists(workspaceRoot + wxFILE_SEP_PATH + wxS("CMakeLists.txt"))) {
        AddUnique(&toolchains_, wxS("CMake"));
        const wxString preset = FirstPresetName(workspaceRoot);
        if (!preset.empty()) {
            tasks_.push_back(Task(wxS("CMake: Configure"), wxS("cmake"),
                                  {wxS("--preset"), preset}, workspaceRoot));
        } else {
            tasks_.push_back(Task(wxS("CMake: Configure"), wxS("cmake"),
                                  {wxS("-S"), workspaceRoot, wxS("-B"), workspaceRoot + wxFILE_SEP_PATH + wxS("build")},
                                  workspaceRoot));
        }
        const wxString presetBuildDirectory = PresetBinaryDirectory(workspaceRoot);
        const wxString buildDirectory = presetBuildDirectory.empty()
            ? workspaceRoot + wxFILE_SEP_PATH + wxS("build") : presetBuildDirectory;
        tasks_.push_back(Task(wxS("CMake: Build"), wxS("cmake"),
                              {wxS("--build"), buildDirectory}, workspaceRoot));
        LoadCMakeTargets(workspaceRoot, buildDirectory);
    }

    const wxString ninjaDirectory = ExistingGeneratorDirectory(workspaceRoot);
    if (!ninjaDirectory.empty() && wxFileExists(ninjaDirectory + wxFILE_SEP_PATH + wxS("build.ninja"))) {
        AddUnique(&toolchains_, wxS("Ninja"));
        tasks_.push_back(ProjectTask{wxS("Ninja: Build"), wxS("ninja"), {}, ninjaDirectory,
                                      wxEmptyString, wxEmptyString, ProjectTaskKind::Build, wxS("Ninja")});
        LoadNinjaTargets(workspaceRoot, ninjaDirectory);
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
    ProjectScheme scheme{name, configuration, target.name, target.toolchain, target.projectFile,
                         wxEmptyString, wxEmptyString, wxEmptyString};
    scheme.buildTaskName = target.buildTaskName;
    scheme.runTaskName = target.supportsRun ? wxString(wxS("Run: ")) + target.name : wxString(wxEmptyString);
    scheme.artifactPath = target.outputPath;
    schemes_.push_back(scheme);
}

void ProjectConfig::AddBuildTaskForTarget(const ProjectTarget& target, const wxString& program,
                                          const wxArrayString& arguments)
{
    if (target.buildTaskName.empty()) return;
    for (const auto& task : tasks_) {
        if (task.name == target.buildTaskName && task.targetName == target.name) return;
    }
    ProjectTask task{target.buildTaskName, program, arguments, target.workingDirectory,
                     target.projectFile, target.name, ProjectTaskKind::Build, target.toolchain};
    tasks_.push_back(task);
}

void ProjectConfig::LoadCMakeTargets(const wxString& workspaceRoot, const wxString& buildDirectory)
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
        target.buildDirectory = buildDirectory;
        target.runProgram = buildDirectory + wxFILE_SEP_PATH + name;
        target.outputPath = target.runProgram;
        target.artifactCandidates.Add(buildDirectory + wxFILE_SEP_PATH + wxS("Debug") + wxFILE_SEP_PATH + name);
        target.artifactCandidates.Add(buildDirectory + wxFILE_SEP_PATH + wxS("Release") + wxFILE_SEP_PATH + name);
        target.artifactCandidates.Add(buildDirectory + wxFILE_SEP_PATH + wxS("bin") + wxFILE_SEP_PATH + name);
        target.artifactCandidates.Add(target.runProgram);
        AddTarget(target);
        wxArrayString arguments;
        arguments.Add(wxS("--build"));
        arguments.Add(buildDirectory);
        arguments.Add(wxS("--target"));
        arguments.Add(name);
        AddBuildTaskForTarget(target, wxS("cmake"), arguments);
    }
    if (targets_.empty() || std::none_of(targets_.begin(), targets_.end(), [](const ProjectTarget& target) {
            return target.toolchain == wxS("CMake");
        })) {
        ProjectTarget aggregate{wxS("CMake:all"), wxS("all"), wxS("CMake"), wxEmptyString,
                                workspaceRoot, wxEmptyString, wxS("CMake: Build"), wxEmptyString, {},
                                ProjectTargetKind::Aggregate, true, false, false, wxEmptyString, {}};
        aggregate.buildDirectory = buildDirectory;
        AddTarget(aggregate);
    }
}

void ProjectConfig::LoadNinjaTargets(const wxString& workspaceRoot, const wxString& buildDirectory)
{
    wxString text;
    if (!ReadText(buildDirectory + wxFILE_SEP_PATH + wxS("build.ninja"), &text)) return;
    const wxString workingDirectory = workspaceRoot.empty() ? buildDirectory : workspaceRoot;
    for (wxString line : wxSplit(text, wxChar('\n'))) {
        line.Trim(true).Trim(false);
        if (!line.StartsWith(wxS("build "))) continue;
        wxString declaration = line.Mid(6);
        const int colon = declaration.Find(wxChar(':'));
        if (colon == wxNOT_FOUND) continue;
        wxString name = declaration.Left(colon);
        name.Trim(true).Trim(false);
        if (name.empty() || name.Contains(wxS("/")) || name.StartsWith(wxS("$"))) continue;
        const wxString rule = declaration.Mid(colon + 1).Lower();
        if (rule.StartsWith(wxS("phony")) || name == wxS("all")) continue;

        const bool executable = rule.Find(wxS("executable")) != wxNOT_FOUND ||
                                rule.Find(wxS("linker")) != wxNOT_FOUND;
        ProjectTarget target;
        target.id = wxS("Ninja:") + name;
        target.name = name;
        target.toolchain = wxS("Ninja");
        target.workingDirectory = workingDirectory;
        target.buildDirectory = buildDirectory;
        target.buildTaskName = wxS("Ninja: Build ") + name;
        target.kind = executable ? ProjectTargetKind::Executable : ProjectTargetKind::Library;
        target.supportsRun = executable;
        target.supportsDebug = executable;
        target.runProgram = buildDirectory + wxFILE_SEP_PATH + name;
        target.outputPath = target.runProgram;
        target.artifactCandidates.Add(target.runProgram);
        target.artifactCandidates.Add(buildDirectory + wxFILE_SEP_PATH + wxS("bin") + wxFILE_SEP_PATH + name);
        AddTarget(target);
        wxArrayString arguments;
        arguments.Add(wxS("-C"));
        arguments.Add(buildDirectory);
        arguments.Add(name);
        AddBuildTaskForTarget(target, wxS("ninja"), arguments);
    }
    if (std::none_of(targets_.begin(), targets_.end(), [](const ProjectTarget& target) {
            return target.toolchain == wxS("Ninja");
        })) {
        ProjectTarget aggregate{wxS("Ninja:all"), wxS("all"), wxS("Ninja"), wxEmptyString,
                               buildDirectory, wxEmptyString, wxS("Ninja: Build"), wxEmptyString, {},
                               ProjectTargetKind::Aggregate, true, false, false, wxEmptyString, {}};
        aggregate.buildDirectory = buildDirectory;
        AddTarget(aggregate);
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
                            ProjectTargetKind::Aggregate, true, false, false, wxEmptyString, {}};
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
                             {}, ProjectTargetKind::Executable, true, true, true, wxEmptyString, {}};
        target.buildDirectory = workspaceRoot + wxFILE_SEP_PATH + wxS("target");
        target.artifactCandidates.Add(workspaceRoot + wxFILE_SEP_PATH + wxS("target") + wxFILE_SEP_PATH + wxS("debug") + wxFILE_SEP_PATH + name);
        target.artifactCandidates.Add(workspaceRoot + wxFILE_SEP_PATH + wxS("target") + wxFILE_SEP_PATH + wxS("release") + wxFILE_SEP_PATH + name);
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
                                ProjectTargetKind::Aggregate, true, false, false, wxEmptyString, {}});
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
                             wxEmptyString, {}, ProjectTargetKind::Script, true, false, false, wxEmptyString, {}};
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
                                ProjectTargetKind::Script, true, false, false, wxEmptyString, {}});
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
                                              toolchain, wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString});
            schemes_.push_back(ProjectScheme{wxString::Format(wxS("Release — %s"), toolchain), wxS("Release"), target,
                                              toolchain, wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString});
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
                              output, {}, ProjectTargetKind::CodeBlocks, true, !output.empty(), !output.empty(), wxEmptyString, {}};
        imported.artifactCandidates.Add(output);
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
                         fields.size() > 5 ? fields[5] : wxString(wxEmptyString),
                         ProjectTaskKind::Generic, wxEmptyString};
        task.kind = fields.size() > 6 ? TaskKindFromValue(fields[6]) : ProjectTaskKind::Generic;
        task.toolchain = fields.size() > 7 && !fields[7].empty() ? fields[7] : wxS("Custom");
        tasks_.push_back(task);
        if (toolchains_.Index(wxS("Custom")) == wxNOT_FOUND) toolchains_.Add(wxS("Custom"));
    }
}

void ProjectConfig::LoadCustomSchemes(const wxString& workspaceRoot)
{
    const wxString path = workspaceRoot + wxFILE_SEP_PATH + wxS(".codium-blocks") + wxFILE_SEP_PATH + wxS("schemes.tsv");
    if (!wxFileExists(path)) return;
    wxString text;
    if (!ReadText(path, &text)) return;
    for (wxString line : wxSplit(text, wxChar('\n'))) {
        if (line.empty() || line.StartsWith(wxS("#"))) continue;
        const wxArrayString fields = TabFields(line);
        if (fields.size() < 4 || fields[0].empty() || fields[1].empty() || fields[2].empty() || fields[3].empty()) continue;
        ProjectScheme scheme{fields[0], fields[1], fields[2], fields[3],
                             fields.size() > 4 ? fields[4] : wxString(wxEmptyString),
                             wxEmptyString, wxEmptyString, wxEmptyString};
        scheme.buildTaskName = fields.size() > 5 ? fields[5] : wxString(wxEmptyString);
        scheme.runTaskName = fields.size() > 6 ? fields[6] : wxString(wxEmptyString);
        scheme.artifactPath = fields.size() > 7 ? fields[7] : wxString(wxEmptyString);
        bool duplicate = false;
        for (const auto& existing : schemes_) {
            if (existing.name == scheme.name) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) schemes_.push_back(scheme);
        if (toolchains_.Index(scheme.toolchain) == wxNOT_FOUND) toolchains_.Add(scheme.toolchain);
    }
}

wxArrayString ProjectConfig::ArtifactCandidates(const ProjectTarget& target, const wxString& configuration,
                                                const wxString& overridePath)
{
    wxArrayString paths;
    AddUniqueTargetPath(&paths, overridePath, target.workingDirectory, configuration);
    for (const auto& candidate : target.artifactCandidates) {
        AddUniqueTargetPath(&paths, candidate, target.workingDirectory, configuration);
    }
    AddUniqueTargetPath(&paths, target.outputPath, target.workingDirectory, configuration);
    AddUniqueTargetPath(&paths, target.runProgram, target.workingDirectory, configuration);
    if (!target.buildDirectory.empty() && !target.name.empty()) {
        AddUniqueTargetPath(&paths, target.buildDirectory + wxFILE_SEP_PATH + configuration + wxFILE_SEP_PATH + target.name,
                            target.workingDirectory, configuration);
        AddUniqueTargetPath(&paths, target.buildDirectory + wxFILE_SEP_PATH + wxS("bin") + wxFILE_SEP_PATH + target.name,
                            target.workingDirectory, configuration);
        AddUniqueTargetPath(&paths, target.buildDirectory + wxFILE_SEP_PATH + target.name,
                            target.workingDirectory, configuration);
    }
    return paths;
}

wxString ProjectConfig::DiscoverArtifact(const ProjectTarget& target, const wxString& configuration,
                                         const wxString& overridePath)
{
    for (const auto& candidate : ArtifactCandidates(target, configuration, overridePath)) {
        if (wxFileExists(candidate)) return candidate;
    }
    return wxEmptyString;
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
