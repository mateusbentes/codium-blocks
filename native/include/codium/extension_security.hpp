#pragma once

#include <wx/string.h>

namespace codium {

struct ExtensionManifest final {
    wxString name;
    wxString publisher;
    wxString version;
    wxString engine;
};

class ExtensionSecurity final {
public:
    static bool ComputeSha256(const wxString& path, wxString* digest, wxString* error = nullptr);
    static bool ValidateManifest(const wxString& json, ExtensionManifest* manifest, wxString* error = nullptr);
    static bool IsAllowedRegistryUrl(const wxString& url);
    static bool IsAllowedScheme(const wxString& url);
};

} // namespace codium
