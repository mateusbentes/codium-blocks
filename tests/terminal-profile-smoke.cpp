#include "codium/terminal_profile.hpp"

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/utils.h>

#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return 1;

    const wxString root = wxFileName::GetTempDir() + wxFILE_SEP_PATH + wxS("codium-blocks-profile-smoke");
    wxFileName::Mkdir(root, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    wxSetEnv(wxS("CODIUM_BLOCKS_DATA"), root);

    codium::TerminalProfile profile;
    profile.shell = wxS("/bin/bash");
    profile.columns = 132;
    profile.rows = 44;
    profile.history.Add(wxS("printf 'hello'"));
    profile.history.Add(wxS("vim demo.txt"));
    wxString error;
    if (!codium::TerminalProfileStore::Save(profile, wxS("smoke"), &error)) {
        std::cerr << "terminal-profile-smoke: save failed: " << error.ToStdString() << "\n";
        return 2;
    }

    const codium::TerminalProfile loaded = codium::TerminalProfileStore::Load(wxS("smoke"));
    if (loaded.shell != profile.shell || loaded.columns != profile.columns || loaded.rows != profile.rows ||
        loaded.history != profile.history) {
        std::cerr << "terminal-profile-smoke: loaded profile differs\n";
        return 3;
    }

    std::cout << "terminal-profile-smoke: ok — shell, dimensions, and history persisted\n";
    return 0;
}
