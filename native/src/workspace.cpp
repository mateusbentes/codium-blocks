#include "codium/workspace.hpp"

#include <wx/dir.h>
#include <wx/filename.h>

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
    Refresh();
    return true;
}

void Workspace::Close()
{
    rootPath_.clear();
    files_.Clear();
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
    const wxFileName path(absolutePath);
    return Contains(kIgnoredDirectoryNames, path.GetFullName());
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
