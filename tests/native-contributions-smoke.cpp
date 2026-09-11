// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/native_contributions.hpp"

#include <wx/filename.h>
#include <wx/init.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return 1;
    codium::TreeViewRegistry trees;
    trees.Register(wxS("dependencies"), wxS("Dependencies"));
    trees.SetItems(wxS("dependencies"), wxArrayString{wxS("wxWidgets"), wxS("Node.js")});
    if (trees.ViewTitles().GetCount() != 1 || trees.Items(wxS("dependencies")).GetCount() != 2) {
        std::cerr << "native-contributions-smoke: tree view registry failed\n";
        return 2;
    }

    const wxString root = wxFileName::GetTempDir() + wxFILE_SEP_PATH + wxS("codium-blocks-contributions-smoke");
    std::filesystem::remove_all(root.ToStdString());
    wxFileName::Mkdir(root, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const std::string initCommand = std::string("git -C \"") + root.ToStdString() + "\" init -q";
    if (std::system(initCommand.c_str()) != 0) {
        std::cerr << "native-contributions-smoke: git init failed\n";
        return 3;
    }
    std::ofstream(root.ToStdString() + "/tracked.txt") << "tracked";
    codium::ScmModel scm;
    wxString error;
    if (!scm.Refresh(root, &error) || !scm.IsRepository()) {
        std::cerr << "native-contributions-smoke: SCM refresh failed: " << error.ToStdString() << "\n";
        return 4;
    }

    codium::CustomEditorRegistry editors;
    editors.Register(wxS(".json"), wxS("json-custom-editor"));
    if (editors.Resolve(wxS("settings.json")) != wxS("json-custom-editor") ||
        !editors.Resolve(wxS("main.cpp")).empty()) {
        std::cerr << "native-contributions-smoke: custom editor resolution failed\n";
        return 5;
    }
    std::filesystem::remove_all(root.ToStdString());
    std::cout << "native-contributions-smoke: ok — Tree Views, SCM, and custom editors\n";
    return 0;
}
