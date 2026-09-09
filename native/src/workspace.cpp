#include "codium/workspace.hpp"

#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>

#include <algorithm>

namespace codium {

namespace {

const wxArrayString kIgnoredDirectoryNames = {
    wxS(".git"), wxS(".hg"), wxS(".svn"), wxS("build"), wxS("out"),
    wxS("node_modules"), wxS(".codium-blocks"), wxS("extensions-installed"),
    wxS(".vs"), wxS(".idea"), wxS("__pycache__")
};

bool Contains(const wxArrayString& values, const wxString& value)
{
    for (const auto& item : values) {
        if (item == value) return true;
    }
    return false;
}

wxString DirectoryBaseName(const wxString& path)
{
    wxString normalized = path;
    while (normalized.length() > 1 &&
           (normalized.EndsWith(wxS("/")) || normalized.EndsWith(wxS("\\")))) {
        normalized.RemoveLast();
    }
    const int slash = normalized.Find(wxChar('/'), true);
    const int backslash = normalized.Find(wxChar('\\'), true);
    const int separator = std::max(slash, backslash);
    return separator == wxNOT_FOUND ? normalized : normalized.Mid(separator + 1);
}

wxString TrustFilePath()
{
    wxString dataRoot;
    if (!wxGetEnv(wxS("CODIUM_BLOCKS_DATA"), &dataRoot) || dataRoot.empty()) {
        dataRoot = wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + wxS("CodiumBlocks");
    }
    wxFileName::Mkdir(dataRoot, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return dataRoot + wxFILE_SEP_PATH + wxS("trusted-workspaces.txt");
}

wxArrayString ReadTrustedWorkspaces()
{
    wxArrayString paths;
    const wxString filePath = TrustFilePath();
    if (!wxFileExists(filePath)) return paths;
    wxFile file;
    if (!file.Open(filePath, wxFile::read)) return paths;
    wxString text;
    if (!file.ReadAll(&text)) return paths;
    wxStringTokenizer tokenizer(text, wxS("\n"), wxTOKEN_STRTOK);
    while (tokenizer.HasMoreTokens()) paths.Add(tokenizer.GetNextToken().Trim(true).Trim(false));
    return paths;
}

} // namespace

bool Workspace::Open(const wxString& rootPath, wxString* error)
{
    wxFileName root(rootPath, wxEmptyString);
    root.MakeAbsolute();
    wxString normalizedRoot = root.GetFullPath();
    while (normalizedRoot.length() > 1 && normalizedRoot.EndsWith(wxFILE_SEP_PATH)) {
        normalizedRoot.RemoveLast();
    }
    if (!wxDirExists(normalizedRoot)) {
        if (error) *error = wxString::Format(wxS("Workspace directory does not exist: %s."), rootPath);
        return false;
    }
    rootPath_ = normalizedRoot;
    trusted_ = Contains(ReadTrustedWorkspaces(), rootPath_);
    Refresh();
    return true;
}

void Workspace::Close()
{
    rootPath_.clear();
    files_.Clear();
    trusted_ = false;
}

bool Workspace::SetTrusted(bool trusted, wxString* error)
{
    if (!IsOpen()) {
        if (error) *error = wxS("Open a workspace before changing trust.");
        return false;
    }
    wxArrayString paths = ReadTrustedWorkspaces();
    wxArrayString updated;
    for (const auto& path : paths) {
        if (path != rootPath_ && !path.empty()) updated.Add(path);
    }
    if (trusted) updated.Add(rootPath_);

    const wxString filePath = TrustFilePath();
    const wxString temporary = filePath + wxS(".tmp");
    wxFile output;
    if (!output.Open(temporary, wxFile::write)) {
        if (error) *error = wxString::Format(wxS("Could not write workspace trust file: %s."), temporary);
        return false;
    }
    wxString content;
    for (const auto& path : updated) content += path + wxS("\n");
    const wxScopedCharBuffer bytes = content.utf8_str();
    if (output.Write(bytes.data(), bytes.length()) != bytes.length() || !output.Close() || !wxRenameFile(temporary, filePath, true)) {
        wxRemoveFile(temporary);
        if (error) *error = wxString::Format(wxS("Could not commit workspace trust file: %s."), filePath);
        return false;
    }
    trusted_ = trusted;
    return true;
}

wxString Workspace::RelativePath(const wxString& absolutePath) const
{
    if (!IsOpen()) return absolutePath;
    wxFileName relative(absolutePath);
    relative.MakeRelativeTo(rootPath_);
    return relative.GetFullPath();
}

bool Workspace::ShouldSkip(const wxString& absolutePath) const
{
    return Contains(kIgnoredDirectoryNames, DirectoryBaseName(absolutePath));
}

void Workspace::Refresh()
{
    files_.Clear();
    if (!IsOpen()) return;

    wxDir directory(rootPath_);
    if (!directory.IsOpened()) return;

    wxString name;
    bool keepGoing = directory.GetFirst(&name, wxEmptyString, wxDIR_FILES | wxDIR_DIRS);
    while (keepGoing) {
        const wxString absolute = rootPath_ + wxFILE_SEP_PATH + name;
        if (ShouldSkip(absolute)) {
            keepGoing = directory.GetNext(&name);
            continue;
        }
        if (wxDirExists(absolute)) {
            Workspace nested;
            if (nested.Open(absolute)) {
                for (const auto& file : nested.Files()) files_.Add(file);
            }
        } else if (wxFileExists(absolute)) {
            files_.Add(absolute);
        }
        keepGoing = directory.GetNext(&name);
    }
    files_.Sort();
}

} // namespace codium
