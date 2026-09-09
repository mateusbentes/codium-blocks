#pragma once

#include "codium/extension_security.hpp"

#include <wx/arrstr.h>
#include <wx/string.h>

namespace codium {

class ExtensionRegistry final {
public:
    bool AddRegistry(const wxString& url, wxString* error = nullptr);
    bool RemoveRegistry(const wxString& url);
    const wxArrayString& Registries() const { return registries_; }
    bool VerifyArtifact(const wxString& path, const wxString& expectedSha256, wxString* error = nullptr) const;

private:
    wxArrayString registries_;
};

} // namespace codium
