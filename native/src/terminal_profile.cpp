#include "codium/terminal_profile.hpp"

#include <wx/fileconf.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

namespace codium {

wxString TerminalProfileStore::ConfigPath()
{
    wxString root;
    if (!wxGetEnv(wxS("CODIUM_BLOCKS_DATA"), &root) || root.empty()) {
#if defined(__WXMSW__)
        root = wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + wxS("CodiumBlocks");
#elif defined(__WXMAC__)
        root = wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + wxS("CodiumBlocks");
#else
        wxString state;
        if (wxGetEnv(wxS("XDG_STATE_HOME"), &state) && !state.empty()) root = state + wxFILE_SEP_PATH + wxS("codium-blocks");
        else root = wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + wxS("codium-blocks");
#endif
    }
    wxFileName::Mkdir(root, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return root + wxFILE_SEP_PATH + wxS("terminal-profiles.conf");
}

TerminalProfile TerminalProfileStore::Load(const wxString& profileName)
{
    TerminalProfile profile;
    wxFileConfig config(wxS("CodiumBlocks"), wxS("CodiumBlocks"), ConfigPath());
    config.SetPath(wxS("profiles/") + profileName);
    config.Read(wxS("shell"), &profile.shell);
    config.Read(wxS("columns"), &profile.columns, profile.columns);
    config.Read(wxS("rows"), &profile.rows, profile.rows);
    long count = 0;
    config.Read(wxS("history-count"), &count, 0L);
    for (long index = 0; index < count; ++index) {
        wxString command;
        if (config.Read(wxString::Format(wxS("history/%ld"), index), &command)) profile.history.Add(command);
    }
    return profile;
}

bool TerminalProfileStore::Save(const TerminalProfile& profile, const wxString& profileName, wxString* error)
{
    const wxString path = ConfigPath();
    wxFileConfig config(wxS("CodiumBlocks"), wxS("CodiumBlocks"), path);
    config.SetPath(wxS("profiles/") + profileName);
    config.Write(wxS("shell"), profile.shell);
    config.Write(wxS("columns"), profile.columns);
    config.Write(wxS("rows"), profile.rows);
    config.Write(wxS("history-count"), static_cast<long>(profile.history.GetCount()));
    for (size_t index = 0; index < profile.history.GetCount(); ++index) {
        config.Write(wxString::Format(wxS("history/%zu"), index), profile.history[index]);
    }
    if (!config.Flush()) {
        if (error) *error = wxString::Format(wxS("Could not save terminal profile: %s."), path);
        return false;
    }
    return true;
}

} // namespace codium
