#include "codium/build_session.hpp"

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

#include <filesystem>
#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return 1;
    const wxString root = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + wxS("codium-blocks-build-session-smoke");
    std::filesystem::remove_all(root.ToStdString());
    wxFileName::Mkdir(root, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    codium::BuildSessionStore store;
    wxString error;
    if (!store.Load(root, &error)) return 2;
    codium::BuildSessionSpec specification;
    specification.taskName = wxS("CMake: Build");
    specification.target = wxS("app");
    specification.configuration = wxS("Debug");
    specification.toolchain = wxS("CMake");
    specification.workingDirectory = root;
    const wxString id = store.Begin(specification, &error);
    if (id.empty() || !store.AppendOutput(id, wxS("warning: src/main.cpp:1:1: test"), &error) ||
        !store.Finish(id, 0, false, &error)) return 3;

    codium::BuildSessionStore reopened;
    if (!reopened.Load(root, &error) || reopened.Sessions().size() != 1) return 4;
    const auto& session = reopened.Sessions().front();
    if (session.taskName != specification.taskName || session.target != specification.target ||
        session.configuration != specification.configuration || !session.Succeeded() ||
        session.output.size() != 1 || session.output[0].Find(wxS("warning")) == wxNOT_FOUND) return 5;

    std::filesystem::remove_all(root.ToStdString());
    std::cout << "build-session-smoke: ok — persistent metadata, output, status, and reload\n";
    return 0;
}
