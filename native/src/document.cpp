#include "codium/document.hpp"

#include <wx/file.h>
#include <wx/filename.h>

namespace codium {

Document::Document(wxString path)
    : path_(std::move(path))
{
}

bool Document::Load(const wxString& path, wxString* error)
{
    wxFile file;
    if (!file.Open(path, wxFile::read)) {
        if (error) {
            *error = wxString::Format(wxS("Could not open %s."), path);
        }
        return false;
    }

    const wxFileOffset length = file.Length();
    if (length < 0 || length > static_cast<wxFileOffset>(0x7fffffff)) {
        if (error) {
            *error = wxString::Format(wxS("File is too large to open: %s."), path);
        }
        return false;
    }

    wxCharBuffer bytes(static_cast<size_t>(length) + 1);
    if (length > 0 && file.Read(bytes.data(), static_cast<size_t>(length)) != length) {
        if (error) {
            *error = wxString::Format(wxS("Could not read %s."), path);
        }
        return false;
    }
    bytes.data()[length] = '\0';

    path_ = path;
    text_ = wxString::FromUTF8(bytes.data());
    if (text_.empty() && length > 0) {
        text_ = wxString(bytes.data(), wxConvAuto());
    }
    dirty_ = false;
    return true;
}

bool Document::Save(wxString* error) const
{
    if (path_.empty()) {
        if (error) {
            *error = wxS("The document has no path yet.");
        }
        return false;
    }

    const wxFileName filename(path_);
    const wxString parentDirectory = filename.GetPath();
    if (!parentDirectory.empty() && !wxDirExists(parentDirectory)) {
        wxFileName::Mkdir(parentDirectory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }

    wxFile file;
    if (!file.Open(path_, wxFile::write)) {
        if (error) {
            *error = wxString::Format(wxS("Could not save %s."), path_);
        }
        return false;
    }

    const wxScopedCharBuffer utf8 = text_.utf8_str();
    const size_t length = utf8.length();
    if (file.Write(utf8.data(), length) != length) {
        if (error) {
            *error = wxString::Format(wxS("Could not write %s."), path_);
        }
        return false;
    }
    return true;
}

void Document::SetText(wxString text)
{
    text_ = std::move(text);
    dirty_ = true;
}

void Document::MarkClean()
{
    dirty_ = false;
}

} // namespace codium
