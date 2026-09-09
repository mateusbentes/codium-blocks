#include "codium/extension_registry.hpp"

#include <wx/filefn.h>

namespace codium {

bool ExtensionRegistry::AddRegistry(const wxString& url, wxString* error)
{
    if (!ExtensionSecurity::IsAllowedRegistryUrl(url)) {
        if (error) *error = wxString::Format(wxS("Registry URL is not allowed: %s. Only HTTPS registries without embedded credentials are accepted."), url);
        return false;
    }
    if (registries_.Index(url) == wxNOT_FOUND) registries_.Add(url);
    return true;
}

bool ExtensionRegistry::RemoveRegistry(const wxString& url)
{
    const int index = registries_.Index(url);
    if (index == wxNOT_FOUND) return false;
    registries_.RemoveAt(static_cast<size_t>(index));
    return true;
}

bool ExtensionRegistry::VerifyArtifact(const wxString& path, const wxString& expectedSha256, wxString* error) const
{
    if (expectedSha256.empty()) {
        if (error) *error = wxS("An expected SHA-256 digest is required for registry artifacts.");
        return false;
    }
    wxString actual;
    if (!ExtensionSecurity::ComputeSha256(path, &actual, error)) return false;
    if (actual.Lower() != expectedSha256.Lower()) {
        if (error) *error = wxString::Format(wxS("Artifact SHA-256 mismatch: expected %s, got %s."), expectedSha256, actual);
        return false;
    }
    return true;
}

} // namespace codium
