#include "codium/vsix_manager.hpp"

#include <wx/dir.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stream.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#include <memory>

namespace codium {

namespace {

bool IsSafeArchivePath(const wxString& name)
{
    if (name.empty() || name.StartsWith(wxS("/") ) || name.StartsWith(wxS("\\")) ||
        name.Find(wxS(":")) != wxNOT_FOUND) {
        return false;
    }

    wxString component;
    for (const auto ch : name) {
        if (ch == wxS('/') || ch == wxS('\\')) {
            if (component == wxS("..")) {
                return false;
            }
            component.clear();
        } else {
            component += ch;
        }
    }
    return component != wxS("..");
}

wxString ArchiveDestination(const wxString& root, const wxString& entryName)
{
    wxString normalized = entryName;
    normalized.Replace(wxS("\\"), wxS("/"));
    return root + wxFILE_SEP_PATH + normalized;
}

} // namespace

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
    const wxString destination = extensionRoot_ + wxFILE_SEP_PATH + source.GetName();
    wxFileName::Mkdir(destination, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    wxFFileInputStream input(vsixPath);
    if (!input.IsOk()) {
        if (message) {
            *message = wxString::Format(wxS("Could not open the VSIX: %s."), vsixPath);
        }
        return false;
    }

    wxZipInputStream archive(input);
    std::unique_ptr<wxZipEntry> entry;
    while ((entry.reset(archive.GetNextEntry()), entry != nullptr)) {
        const wxString entryName = entry->GetName();
        if (!IsSafeArchivePath(entryName)) {
            wxFileName::Rmdir(destination, wxPATH_RMDIR_RECURSIVE);
            if (message) {
                *message = wxString::Format(wxS("Rejected unsafe VSIX path: %s."), entryName);
            }
            return false;
        }

        const wxString outputPath = ArchiveDestination(destination, entryName);
        if (entry->IsDir() || entryName.EndsWith(wxS("/")) || entryName.EndsWith(wxS("\\"))) {
            if (!wxFileName::Mkdir(outputPath, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) &&
                !wxDirExists(outputPath)) {
                if (message) {
                    *message = wxString::Format(wxS("Could not create VSIX directory: %s."), outputPath);
                }
                return false;
            }
            continue;
        }

        const wxFileName outputFile(outputPath);
        if (!wxFileName::Mkdir(outputFile.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) &&
            !wxDirExists(outputFile.GetPath())) {
            if (message) {
                *message = wxString::Format(wxS("Could not create VSIX directory: %s."), outputFile.GetPath());
            }
            return false;
        }

        wxFFileOutputStream output(outputPath);
        if (!output.IsOk()) {
            if (message) {
                *message = wxString::Format(wxS("Could not create VSIX file: %s."), outputPath);
            }
            return false;
        }
        archive.Read(output);
        if (!output.IsOk()) {
            if (message) {
                *message = wxString::Format(wxS("Could not extract VSIX file: %s."), outputPath);
            }
            return false;
        }
    }

    if (archive.GetLastError() != wxSTREAM_NO_ERROR && archive.GetLastError() != wxSTREAM_EOF) {
        if (message) {
            *message = wxS("The VSIX archive is invalid or could not be read.");
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
