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
    if (scm.ChangeList().size() != 1 || scm.ChangeList()[0].kind != codium::ScmChangeKind::Untracked ||
        scm.ChangeList()[0].path != wxS("tracked.txt")) {
        std::cerr << "native-contributions-smoke: SCM status parsing failed\n";
        return 5;
    }
    if (scm.Stage(wxS("../escape.txt"), &error) || scm.Discard(wxS("/tmp/escape.txt"), &error)) {
        std::cerr << "native-contributions-smoke: SCM accepted a path outside the workspace\n";
        return 6;
    }
    if (!scm.Stage(wxS("tracked.txt"), &error) || !scm.Refresh(root, &error) || scm.ChangeList().size() != 1 ||
        !scm.ChangeList()[0].staged || scm.ChangeList()[0].kind != codium::ScmChangeKind::Added) {
        std::cerr << "native-contributions-smoke: SCM stage failed: " << error.ToStdString() << "\n";
        return 7;
    }
    std::ofstream(root.ToStdString() + "/tracked.txt", std::ios::app) << "\nchanged";
    if (!scm.Refresh(root, &error) || scm.ChangeList().size() != 1 || !scm.ChangeList()[0].staged ||
        !scm.ChangeList()[0].worktree || scm.ChangeList()[0].kind != codium::ScmChangeKind::Added) {
        std::cerr << "native-contributions-smoke: SCM staged/worktree status failed\n";
        return 8;
    }
    if (!scm.Discard(wxS("tracked.txt"), &error) || !scm.Refresh(root, &error) ||
        scm.ChangeList().size() != 1 || !scm.ChangeList()[0].staged) {
        std::cerr << "native-contributions-smoke: SCM discard failed: " << error.ToStdString() << "\n";
        return 9;
    }

    codium::CustomEditorRegistry editors;
    editors.Register(wxS(".json"), wxS("json-custom-editor"), wxS("JSON editor"), 10, true);
    if (editors.Resolve(wxS("settings.json")) != wxS("json-custom-editor") ||
        !editors.Resolve(wxS("main.cpp")).empty() || editors.Descriptors().size() != 1 ||
        !editors.Descriptors()[0].supportsText || editors.Descriptors()[0].priority != 10) {
        std::cerr << "native-contributions-smoke: custom editor resolution failed\n";
        return 10;
    }
    if (!codium::WebviewResourcePolicy::IsAllowedUri(wxS("file://") + root + wxS("/view.html"), root) ||
        codium::WebviewResourcePolicy::IsAllowedUri(wxS("https://example.com/view.html"), root) ||
        codium::WebviewResourcePolicy::SanitizeHtml(wxS("<script>alert(1)</script><p onclick=\"x\">javascript:bad</p>")).Find(wxS("<script")) != wxNOT_FOUND ||
        codium::WebviewResourcePolicy::SanitizeHtml(wxS("<p onclick=\"x\">bad</p>")).Find(wxS("onclick")) != wxNOT_FOUND) {
        std::cerr << "native-contributions-smoke: webview resource policy failed\n";
        return 11;
    }
    std::filesystem::remove_all(root.ToStdString());
    std::cout << "native-contributions-smoke: ok — Tree Views, SCM, and custom editors\n";
    return 0;
}
