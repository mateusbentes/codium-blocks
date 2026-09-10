#include "codium/project_config.hpp"

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return 1;

    const wxString root = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + wxS("codium-blocks-target-discovery-smoke");
    std::filesystem::remove_all(root.ToStdString());
    wxFileName::Mkdir(root, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    wxFileName::Mkdir(root + wxFILE_SEP_PATH + wxS(".codium-blocks"), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    wxFileName::Mkdir(root + wxFILE_SEP_PATH + wxS("preset-build"), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    wxFileName::Mkdir(root + wxFILE_SEP_PATH + wxS("build"), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    std::ofstream(root.ToStdString() + "/CMakeLists.txt")
        << "cmake_minimum_required(VERSION 3.20)\nproject(discovery LANGUAGES CXX)\nadd_executable(app main.cpp)\n";
    std::ofstream(root.ToStdString() + "/CMakePresets.json")
        << "{\"version\": 6, \"configurePresets\": [{\"name\": \"debug\", \"binaryDir\": \"${sourceDir}/preset-build\"}]}\n";
    std::ofstream(root.ToStdString() + "/main.cpp") << "int main() { return 0; }\n";
    std::ofstream(root.ToStdString() + "/build/build.ninja")
        << "build ninja-app: CXX_EXECUTABLE_LINKER__ninja-app_ main.o\n";
    std::ofstream(root.ToStdString() + "/Cargo.toml")
        << "[package]\nname = \"discovery\"\nversion = \"0.1.0\"\n\n[[bin]]\nname = \"hello\"\npath = \"src/main.rs\"\n";
    std::ofstream(root.ToStdString() + "/package.json")
        << "{\n  \"scripts\": {\n    \"build\": \"echo build\",\n    \"start\": \"echo start\"\n  }\n}\n";

    const std::string separator(1, '\x1f');
    std::ofstream(root.ToStdString() + "/.codium-blocks/tasks.tsv")
        << "Custom build\techo\tcustom" << separator << "build-ok\t" << root.ToStdString()
        << "\t\tapp\tbuild\tCustom\n"
        << "Custom run\techo\trun-ok\t" << root.ToStdString()
        << "\t\tapp\trun\tCustom\n";
    std::ofstream(root.ToStdString() + "/.codium-blocks/schemes.tsv")
        << "User Debug\tDebug\tapp\tCMake\t\tCustom build\tCustom run\tpreset-build/app\n";
    std::ofstream(root.ToStdString() + "/preset-build/app") << "artifact\n";

    codium::ProjectConfig config;
    wxString error;
    if (!config.Load(root, &error)) {
        std::cerr << "target-discovery-smoke: load failed: " << error.ToStdString() << "\n";
        return 2;
    }

    bool foundPresetConfigure = false;
    bool foundNinjaTarget = false;
    bool foundCustomBuild = false;
    bool foundCustomRun = false;
    for (const auto& task : config.Tasks()) {
        if (task.name == wxS("CMake: Configure") && task.arguments.Index(wxS("--preset")) != wxNOT_FOUND) {
            foundPresetConfigure = true;
        }
        if (task.name == wxS("Custom build") && task.kind == codium::ProjectTaskKind::Build) foundCustomBuild = true;
        if (task.name == wxS("Custom run") && task.kind == codium::ProjectTaskKind::Run) foundCustomRun = true;
    }
    for (const auto& target : config.Targets()) {
        if (target.toolchain == wxS("Ninja") && target.name == wxS("ninja-app")) foundNinjaTarget = true;
    }
    if (!foundPresetConfigure || !foundNinjaTarget || !foundCustomBuild || !foundCustomRun ||
        config.Toolchains().Index(wxS("Ninja")) == wxNOT_FOUND) {
        std::cerr << "target-discovery-smoke: discovery failed\n";
        return 3;
    }

    bool foundScheme = false;
    for (const auto& scheme : config.Schemes()) {
        if (scheme.name == wxS("User Debug") && scheme.buildTaskName == wxS("Custom build") &&
            scheme.runTaskName == wxS("Custom run") && scheme.artifactPath == wxS("preset-build/app")) {
            foundScheme = true;
        }
    }
    if (!foundScheme) {
        std::cerr << "target-discovery-smoke: custom scheme failed\n";
        return 4;
    }

    codium::ProjectTarget artifactTarget;
    artifactTarget.name = wxS("app");
    artifactTarget.workingDirectory = root;
    artifactTarget.buildDirectory = root + wxFILE_SEP_PATH + wxS("preset-build");
    artifactTarget.artifactCandidates.Add(root + wxFILE_SEP_PATH + wxS("missing-app"));
    const wxString artifact = codium::ProjectConfig::DiscoverArtifact(artifactTarget, wxS("Debug"));
    const wxString expectedArtifact = root + wxFILE_SEP_PATH + wxS("preset-build") + wxFILE_SEP_PATH + wxS("app");
    if (artifact != expectedArtifact) {
        std::cerr << "target-discovery-smoke: artifact discovery failed: " << artifact.ToStdString() << "\n";
        return 5;
    }

    std::filesystem::remove_all(root.ToStdString());
    std::cout << "target-discovery-smoke: ok — presets, Ninja, custom tasks/schemes, and artifact discovery\n";
    return 0;
}
