#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

namespace codium {

class VsixManager final {
public:
    explicit VsixManager(wxString extensionRoot);

    bool Install(const wxString& vsixPath, wxString* message = nullptr);
    wxArrayString ListInstalled() const;
    const wxString& ExtensionRoot() const { return extensionRoot_; }

private:
    wxString extensionRoot_;
};

} // namespace codium
