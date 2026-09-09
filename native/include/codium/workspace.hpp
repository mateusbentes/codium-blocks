#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

namespace codium {

class Workspace final {
public:
    bool Open(const wxString& rootPath, wxString* error = nullptr);
    void Close();
    void Refresh();

    bool IsOpen() const { return !rootPath_.empty(); }
    const wxString& RootPath() const { return rootPath_; }
    const wxArrayString& Files() const { return files_; }
    wxString RelativePath(const wxString& absolutePath) const;

private:
    bool ShouldSkip(const wxString& absolutePath) const;

    wxString rootPath_;
    wxArrayString files_;
};

} // namespace codium
