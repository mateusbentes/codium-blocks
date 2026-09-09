#include "codium/workspace.hpp"

#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/init.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

wxString PortableRelative(wxString value)
{
    value.Replace(wxS("\\"), wxS("/"));
    return value;
}

bool HasRelativeFile(const codium::Workspace& workspace, const wxString& expected)
{
    for (const auto& file : workspace.Files()) {
        if (PortableRelative(workspace.RelativePath(file)) == expected) return true;
    }
    return false;
}

} // namespace

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "workspace-smoke: wxWidgets initialization failed\n";
        return 1;
    }

    const wxString root = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + wxS("codium-blocks-workspace-smoke");
    const wxString dataRoot = root + wxFILE_SEP_PATH + wxS("data");
    wxSetEnv(wxS("CODIUM_BLOCKS_DATA"), dataRoot);
    std::filesystem::remove_all(root.ToStdString());
    wxFileName::Mkdir(root + wxFILE_SEP_PATH + wxS("src"), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    wxFileName::Mkdir(root + wxFILE_SEP_PATH + wxS("build"), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    wxFileName::Mkdir(root + wxFILE_SEP_PATH + wxS("node_modules/pkg"), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    std::ofstream(root.ToStdString() + "/README.md") << "workspace";
    std::ofstream(root.ToStdString() + "/src/main.cpp") << "int main() {}";
    std::ofstream(root.ToStdString() + "/build/generated.cpp") << "generated";
    std::ofstream(root.ToStdString() + "/node_modules/pkg/index.js") << "dependency";

    codium::Workspace workspace;
    wxString error;
    if (!workspace.Open(root, &error)) {
        std::cerr << "workspace-smoke: open failed\n";
        return 1;
    }

    const wxString source = root + wxFILE_SEP_PATH + wxS("src/main.cpp");
    if (!HasRelativeFile(workspace, wxS("src/main.cpp")) ||
        !HasRelativeFile(workspace, wxS("README.md")) ||
        HasRelativeFile(workspace, wxS("build/generated.cpp")) ||
        HasRelativeFile(workspace, wxS("node_modules/pkg/index.js")) ||
        PortableRelative(workspace.RelativePath(source)) != wxS("src/main.cpp")) {
        std::cerr << "workspace-smoke: discovery or filtering failed\n";
        std::cerr << "workspace files: " << workspace.Files().GetCount() << "\n";
        for (const auto& file : workspace.Files()) {
            std::cerr << "  " << PortableRelative(workspace.RelativePath(file)).ToStdString() << "\n";
        }
        return 1;
    }
    if (workspace.IsTrusted() || !workspace.SetTrusted(true, &error)) {
        std::cerr << "workspace-smoke: trust enable failed: " << error.ToStdString() << "\n";
        return 1;
    }
    workspace.Close();
    codium::Workspace reopened;
    if (!reopened.Open(root, &error) || !reopened.IsTrusted() || !reopened.SetTrusted(false, &error)) {
        std::cerr << "workspace-smoke: trust persistence failed: " << error.ToStdString() << "\n";
        return 1;
    }

    std::filesystem::remove_all(root.ToStdString());
    std::cout << "workspace-smoke: ok — recursive discovery, filtering, and relative paths\n";
    return 0;
}
