// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/project_config.hpp"
#include "codium/task_runner.hpp"

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "task-smoke: wxWidgets initialization failed\n";
        return 1;
    }

    const wxString root = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + wxS("codium-blocks-task-smoke");
    std::filesystem::remove_all(root.ToStdString());
    wxFileName::Mkdir(root, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    wxFileName::Mkdir(root + wxFILE_SEP_PATH + wxS("build"), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    std::ofstream(root.ToStdString() + "/CMakeLists.txt")
        << "cmake_minimum_required(VERSION 3.20)\nproject(demo LANGUAGES CXX)\nadd_executable(demo main.cpp)\n";
    std::ofstream(root.ToStdString() + "/main.cpp") << "int main() { return 0; }\n";
    std::ofstream(root.ToStdString() + "/Makefile") << "all:\n\t@printf task-ok\\n\n";
    std::ofstream(root.ToStdString() + "/demo.cbp")
        << R"xml(<?xml version="1.0" encoding="UTF-8"?>
<CodeBlocks_project_file>
  <FileVersion major="1" minor="6" />
  <Project>
    <Option title="Demo" />
    <Option compiler="gcc" />
    <Build>
      <Target title="app">
        <Option output="bin/app" working_dir="run" compiler="gcc" />
      </Target>
      <Target title="tests">
        <Option output="bin/tests" compiler="clang" />
      </Target>
    </Build>
  </Project>
</CodeBlocks_project_file>
)xml";

    codium::ProjectConfig config;
    wxString error;
    if (!config.Load(root, &error) || config.Tasks().size() < 6 || config.Schemes().size() < 8 ||
        config.Toolchains().Index(wxS("CMake")) == wxNOT_FOUND || config.Toolchains().Index(wxS("Make")) == wxNOT_FOUND ||
        config.Schemes()[0].configuration != wxS("Debug") || config.Schemes()[1].configuration != wxS("Release")) {
        std::cerr << "task-smoke: project detection failed\n";
        return 1;
    }

    bool foundCodeBlocksApp = false;
    bool foundCodeBlocksTests = false;
    bool foundCMakeDemo = false;
    bool foundCMakeTargetArgument = false;
    for (const auto& task : config.Tasks()) {
        if (task.targetName == wxS("app") && task.program == wxS("codeblocks") && task.projectFile.EndsWith(wxS("demo.cbp"))) {
            foundCodeBlocksApp = true;
        }
        if (task.targetName == wxS("tests")) {
            for (const auto& argument : task.arguments) {
                if (argument == wxS("--target=tests")) foundCodeBlocksTests = true;
            }
        }
        if (task.targetName == wxS("demo") && task.program == wxS("cmake")) {
            foundCMakeDemo = true;
            for (const auto& argument : task.arguments) {
                if (argument == wxS("demo")) foundCMakeTargetArgument = true;
            }
        }
    }
    if (!foundCodeBlocksApp || !foundCodeBlocksTests || !foundCMakeDemo || !foundCMakeTargetArgument ||
        config.Toolchains().Index(wxS("Code::Blocks (gcc)")) == wxNOT_FOUND ||
        config.Toolchains().Index(wxS("Code::Blocks (clang)")) == wxNOT_FOUND) {
        std::cerr << "task-smoke: Code::Blocks project import failed\n";
        return 2;
    }
    codium::ProjectPreferences savedPreferences;
    savedPreferences.schemeName = wxS("Debug — CMake — demo");
    savedPreferences.configuration = wxS("Debug");
    savedPreferences.target = wxS("demo");
    savedPreferences.toolchain = wxS("CMake");
    if (!config.SavePreferences(root, savedPreferences, &error)) return 2;
    codium::ProjectPreferences loadedPreferences;
    if (!config.LoadPreferences(root, &loadedPreferences, &error) ||
        loadedPreferences.schemeName != savedPreferences.schemeName ||
        loadedPreferences.target != savedPreferences.target) return 2;

    wxSetEnv(wxS("CODIUM_BLOCKS_TASK_TEST"), wxS("environment-ok"));
    codium::ProjectTask expansionTask;
    expansionTask.program = wxS("${env:CODIUM_BLOCKS_TASK_TEST}");
    expansionTask.arguments = {wxS("${workspaceFolder}"), wxS("${cwd}"), wxS("${configuration}"), wxS("${target}"), wxS("$ENV{CODIUM_BLOCKS_TASK_TEST}")};
    expansionTask.workingDirectory = root + wxFILE_SEP_PATH + wxS("build");
    expansionTask.configuration = wxS("Release");
    expansionTask.targetName = wxS("demo");
    expansionTask.toolchain = wxS("CMake");
    const codium::ProjectTask expanded = codium::ProjectConfig::ExpandTask(
        expansionTask, wxEmptyString, wxEmptyString, wxEmptyString, wxEmptyString, root);
    if (expanded.program != wxS("environment-ok") || expanded.arguments.size() != 5 ||
        expanded.arguments[0] != root || expanded.arguments[1] != root + wxFILE_SEP_PATH + wxS("build") ||
        expanded.arguments[2] != wxS("Release") || expanded.arguments[3] != wxS("demo") ||
        expanded.arguments[4] != wxS("environment-ok") ||
        expanded.workingDirectory != root + wxFILE_SEP_PATH + wxS("build")) {
        std::cerr << "task-smoke: variable expansion failed\n";
        return 3;
    }

    codium::ProjectTask task;
    task.name = wxS("test task");
    task.program = wxS("cmake");
    task.arguments = {wxS("-E"), wxS("echo"), wxS("${workspaceFolder}"), wxS("${cwd}")};
    task.workingDirectory = root + wxFILE_SEP_PATH + wxS("build");
    codium::TaskRunner runner(nullptr, wxID_HIGHEST + 500);
    if (!runner.Run(task, &error, root)) {
        std::cerr << "task-smoke: task launch failed: " << error.ToStdString() << "\n";
        return 4;
    }

    wxArrayString output;
    for (int i = 0; i < 100 && runner.IsRunning(); ++i) {
        wxMilliSleep(10);
        const wxArrayString lines = runner.Poll();
        for (const auto& line : lines) output.Add(line);
    }
    for (const auto& line : runner.Poll()) output.Add(line);
    bool sawOutput = false;
    const wxString expectedOutput = root + wxS(" ") + root + wxFILE_SEP_PATH + wxS("build");
    for (const auto& line : output) if (line == expectedOutput) sawOutput = true;
    if (!sawOutput || runner.LastExitCode() != 0) {
        std::cerr << "task-smoke: output or exit code failed\n";
        return 5;
    }

    std::filesystem::remove_all(root.ToStdString());
    std::cout << "task-smoke: ok — project detection, Code::Blocks import, and async task execution\n";
    return 0;
}
