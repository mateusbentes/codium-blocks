#include "codium/vsix_manager.hpp"
#include "codium/extension_security.hpp"
#include "codium/signature_verifier.hpp"

#include <wx/dir.h>
#include <wx/file.h>
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
    if (name.empty() || name.StartsWith(wxS("/")) || name.StartsWith(wxS("\\")) ||
        name.Find(wxS(":")) != wxNOT_FOUND) return false;

    wxString component;
    for (const auto ch : name) {
        if (ch == wxS('/') || ch == wxS('\\')) {
            if (component == wxS("..")) return false;
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

bool ReadTextFile(const wxString& path, wxString* text)
{
    wxFile file;
    if (!file.Open(path, wxFile::read)) return false;
    return file.ReadAll(text);
}

bool WriteTextFile(const wxString& path, const wxString& text)
{
    wxFile file;
    if (!file.Open(path, wxFile::write)) return false;
    const wxScopedCharBuffer bytes = text.utf8_str();
    return file.Write(bytes.data(), bytes.length()) == bytes.length();
}

} // namespace

VsixManager::VsixManager(wxString extensionRoot)
    : extensionRoot_(std::move(extensionRoot))
{
    wxFileName::Mkdir(extensionRoot_, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
}

bool VsixManager::Install(const wxString& vsixPath, wxString* message)
{
    return InstallVerified(vsixPath, wxEmptyString, message);
}

bool VsixManager::InstallSigned(const wxString& vsixPath, const wxString& expectedSha256,
                                const wxString& publicKeyHex, const wxString& signatureHex,
                                wxString* message)
{
    wxString signatureError;
    if (!SignatureVerifier::VerifyEd25519File(vsixPath, publicKeyHex, signatureHex, &signatureError)) {
        if (message) *message = signatureError;
        return false;
    }
    return InstallVerified(vsixPath, expectedSha256, message);
}

bool VsixManager::InstallVerified(const wxString& vsixPath, const wxString& expectedSha256, wxString* message)
{
    if (!wxFileExists(vsixPath) || !vsixPath.Lower().EndsWith(wxS(".vsix"))) {
        if (message) *message = wxS("The file must exist and have a .vsix extension.");
        return false;
    }

    wxString actualSha256;
    wxString securityError;
    if (!ExtensionSecurity::ComputeSha256(vsixPath, &actualSha256, &securityError)) {
        if (message) *message = securityError;
        return false;
    }
    if (!expectedSha256.empty() && actualSha256.Lower() != expectedSha256.Lower()) {
        if (message) *message = wxString::Format(wxS("VSIX SHA-256 mismatch: expected %s, got %s."), expectedSha256, actualSha256);
        return false;
    }

    const wxFileName source(vsixPath);
    const wxString destination = extensionRoot_ + wxFILE_SEP_PATH + source.GetName();
    const wxString staging = extensionRoot_ + wxFILE_SEP_PATH + wxS(".staging-") + source.GetName();
    const wxString rollback = extensionRoot_ + wxFILE_SEP_PATH + wxS(".rollback-") + source.GetName();
    wxFileName::Rmdir(staging, wxPATH_RMDIR_RECURSIVE);
    wxFileName::Rmdir(rollback, wxPATH_RMDIR_RECURSIVE);
    if (!wxFileName::Mkdir(staging, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        if (message) *message = wxString::Format(wxS("Could not create VSIX staging directory: %s."), staging);
        return false;
    }

    wxFFileInputStream input(vsixPath);
    if (!input.IsOk()) {
        wxFileName::Rmdir(staging, wxPATH_RMDIR_RECURSIVE);
        if (message) *message = wxString::Format(wxS("Could not open the VSIX: %s."), vsixPath);
        return false;
    }

    wxZipInputStream archive(input);
    std::unique_ptr<wxZipEntry> entry;
    while ((entry.reset(archive.GetNextEntry()), entry != nullptr)) {
        const wxString entryName = entry->GetName();
        if (!IsSafeArchivePath(entryName)) {
            wxFileName::Rmdir(staging, wxPATH_RMDIR_RECURSIVE);
            if (message) *message = wxString::Format(wxS("Rejected unsafe VSIX path: %s."), entryName);
            return false;
        }

        const wxString outputPath = ArchiveDestination(staging, entryName);
        if (entry->IsDir() || entryName.EndsWith(wxS("/")) || entryName.EndsWith(wxS("\\"))) {
            if (!wxFileName::Mkdir(outputPath, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) && !wxDirExists(outputPath)) {
                wxFileName::Rmdir(staging, wxPATH_RMDIR_RECURSIVE);
                if (message) *message = wxString::Format(wxS("Could not create VSIX directory: %s."), outputPath);
                return false;
            }
            continue;
        }

        const wxFileName outputFile(outputPath);
        if (!wxFileName::Mkdir(outputFile.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) && !wxDirExists(outputFile.GetPath())) {
            wxFileName::Rmdir(staging, wxPATH_RMDIR_RECURSIVE);
            if (message) *message = wxString::Format(wxS("Could not create VSIX directory: %s."), outputFile.GetPath());
            return false;
        }

        wxFFileOutputStream output(outputPath);
        if (!output.IsOk()) {
            wxFileName::Rmdir(staging, wxPATH_RMDIR_RECURSIVE);
            if (message) *message = wxString::Format(wxS("Could not create VSIX file: %s."), outputPath);
            return false;
        }
        archive.Read(output);
        if (!output.IsOk()) {
            wxFileName::Rmdir(staging, wxPATH_RMDIR_RECURSIVE);
            if (message) *message = wxString::Format(wxS("Could not extract VSIX file: %s."), outputPath);
            return false;
        }
    }

    if (archive.GetLastError() != wxSTREAM_NO_ERROR && archive.GetLastError() != wxSTREAM_EOF) {
        wxFileName::Rmdir(staging, wxPATH_RMDIR_RECURSIVE);
        if (message) *message = wxS("The VSIX archive is invalid or could not be read.");
        return false;
    }

    wxString manifestJson;
    wxString manifestPath = staging + wxFILE_SEP_PATH + wxS("extension") + wxFILE_SEP_PATH + wxS("package.json");
    if (!wxFileExists(manifestPath)) manifestPath = staging + wxFILE_SEP_PATH + wxS("package.json");
    ExtensionManifest manifest;
    if (!wxFileExists(manifestPath) || !ReadTextFile(manifestPath, &manifestJson) ||
        !ExtensionSecurity::ValidateManifest(manifestJson, &manifest, &securityError)) {
        wxFileName::Rmdir(staging, wxPATH_RMDIR_RECURSIVE);
        if (message) *message = securityError.empty() ? wxS("VSIX does not contain a valid package.json manifest.") : securityError;
        return false;
    }

    const wxString metadata = wxString::Format(
        wxS("{\"name\":\"%s\",\"publisher\":\"%s\",\"version\":\"%s\",\"sha256\":\"%s\",\"trusted\":false}\n"),
        manifest.name, manifest.publisher, manifest.version, actualSha256);
    if (!WriteTextFile(staging + wxFILE_SEP_PATH + wxS(".codium-manifest.json"), metadata)) {
        wxFileName::Rmdir(staging, wxPATH_RMDIR_RECURSIVE);
        if (message) *message = wxS("Could not write extension security metadata.");
        return false;
    }

    if (wxDirExists(destination)) {
        if (!wxRenameFile(destination, rollback, true)) {
            wxFileName::Rmdir(staging, wxPATH_RMDIR_RECURSIVE);
            if (message) *message = wxS("Could not stage the existing extension for rollback.");
            return false;
        }
    }
    if (!wxRenameFile(staging, destination, true)) {
        if (wxDirExists(rollback)) wxRenameFile(rollback, destination, true);
        wxFileName::Rmdir(staging, wxPATH_RMDIR_RECURSIVE);
        if (message) *message = wxS("Could not commit the extension installation.");
        return false;
    }
    wxFileName::Rmdir(rollback, wxPATH_RMDIR_RECURSIVE);

    if (message) *message = wxString::Format(wxS("Extension %s.%s@%s installed at %s (SHA-256 %s)."),
                                             manifest.publisher, manifest.name, manifest.version, destination, actualSha256);
    return true;
}

wxArrayString VsixManager::ListInstalled() const
{
    wxArrayString result;
    wxDir dir(extensionRoot_);
    if (!dir.IsOpened()) return result;

    wxString name;
    bool keepGoing = dir.GetFirst(&name, wxEmptyString, wxDIR_DIRS);
    while (keepGoing) {
        if (!name.StartsWith(wxS("."))) result.Add(name);
        keepGoing = dir.GetNext(&name);
    }
    return result;
}

} // namespace codium
