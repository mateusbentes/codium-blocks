#include "codium/workspace.hpp"

#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "workspace-smoke: wxWidgets initialization failed\n";
        return 1;
    }

    const wxString root = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + wxS("codium-blocks-workspace-smoke");
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
    const wxString readme = root + wxFILE_SEP_PATH + wxS("README.md");
    if (workspace.Files().Index(source) == wxNOT_FOUND ||
        workspace.Files().Index(readme) == wxNOT_FOUND ||
        workspace.Files().Index(root + wxFILE_SEP_PATH + wxS("build/generated.cpp")) != wxNOT_FOUND ||
        workspace.Files().Index(root + wxFILE_SEP_PATH + wxS("node_modules/pkg/index.js")) != wxNOT_FOUND ||
        workspace.RelativePath(source) != wxS("src/main.cpp")) {
        std::cerr << "workspace-smoke: discovery or filtering failed\n";
        return 1;
    }

    std::filesystem::remove_all(root.ToStdString());
    std::cout << "workspace-smoke: ok — recursive discovery, filtering, and relative paths\n";
    return 0;
}
