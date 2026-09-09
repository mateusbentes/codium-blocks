#include "codium/project_config.hpp"
#include "codium/task_runner.hpp"

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

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
    std::ofstream(root.ToStdString() + "/CMakeLists.txt") << "cmake_minimum_required(VERSION 3.20)\n";
    std::ofstream(root.ToStdString() + "/Makefile") << "all:\n\t@printf task-ok\\n\n";

    codium::ProjectConfig config;
    wxString error;
    if (!config.Load(root, &error) || config.Tasks().size() < 3 || config.Schemes().size() < 4 ||
        config.Toolchains().Index(wxS("CMake")) == wxNOT_FOUND || config.Toolchains().Index(wxS("Make")) == wxNOT_FOUND ||
        config.Schemes()[0].configuration != wxS("Debug") || config.Schemes()[1].configuration != wxS("Release")) {
        std::cerr << "task-smoke: project detection failed\n";
        return 1;
    }

    codium::ProjectTask task;
    task.name = wxS("test task");
    task.program = wxS("cmake");
    task.arguments = {wxS("-E"), wxS("echo"), wxS("task-ok")};
    task.workingDirectory = root;
    codium::TaskRunner runner(nullptr, wxID_HIGHEST + 500);
    if (!runner.Run(task, &error)) {
        std::cerr << "task-smoke: task launch failed: " << error.ToStdString() << "\n";
        return 1;
    }

    wxArrayString output;
    for (int i = 0; i < 100 && runner.IsRunning(); ++i) {
        wxMilliSleep(10);
        const wxArrayString lines = runner.Poll();
        for (const auto& line : lines) output.Add(line);
    }
    for (const auto& line : runner.Poll()) output.Add(line);
    bool sawOutput = false;
    for (const auto& line : output) if (line == wxS("task-ok")) sawOutput = true;
    if (!sawOutput || runner.LastExitCode() != 0) {
        std::cerr << "task-smoke: output or exit code failed\n";
        return 1;
    }

    std::filesystem::remove_all(root.ToStdString());
    std::cout << "task-smoke: ok — project detection and async task execution\n";
    return 0;
}
