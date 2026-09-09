#include "codium/vsix_manager.hpp"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/utils.h>

namespace codium {

VsixManager::VsixManager(wxString extensionRoot)
    : extensionRoot_(std::move(extensionRoot))
{
    wxFileName::Mkdir(extensionRoot_, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
}

bool VsixManager::Install(const wxString& vsixPath, wxString* message)
{
    if (!wxFileExists(vsixPath) || !vsixPath.Lower().EndsWith(wxS(".vsix"))) {
        if (message) {
            *message = wxS("The file must exist and have a .vsix extension.");
        }
        return false;
    }

    wxFileName source(vsixPath);
    wxString destination = extensionRoot_ + wxFILE_SEP_PATH + source.GetName();
    wxFileName::Mkdir(destination, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    // A VSIX is a ZIP archive. The first version uses the system unzip utility
    // to keep the core small; package validation will be added before production.
    const wxString command = wxString::Format(
        wxS("unzip -q -o \"%s\" -d \"%s\""), vsixPath, destination);
    const int exitCode = wxExecute(command, wxEXEC_SYNC);
    if (exitCode != 0) {
        if (message) {
            *message = wxString::Format(wxS("Failed to extract the VSIX (exit code %d)."), exitCode);
        }
        return false;
    }

    if (message) {
        *message = wxString::Format(wxS("Extension installed at %s"), destination);
    }
    return true;
}

wxArrayString VsixManager::ListInstalled() const
{
    wxArrayString result;
    wxDir dir(extensionRoot_);
    if (!dir.IsOpened()) {
        return result;
    }

    wxString name;
    bool keepGoing = dir.GetFirst(&name, wxEmptyString, wxDIR_DIRS);
    while (keepGoing) {
        result.Add(name);
        keepGoing = dir.GetNext(&name);
    }
    return result;
}

} // namespace codium
