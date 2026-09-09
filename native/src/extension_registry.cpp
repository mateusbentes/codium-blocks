#include "codium/extension_registry.hpp"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/sstream.h>
#include <wx/stdpaths.h>
#include <wx/url.h>
#include <wx/utils.h>

#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>

namespace codium {

namespace {

wxString EncodeQuery(const wxString& input)
{
    const wxScopedCharBuffer bytes = input.utf8_str();
    static const char hex[] = "0123456789ABCDEF";
    wxString result;
    for (size_t index = 0; index < bytes.length(); ++index) {
        const unsigned char value = static_cast<unsigned char>(bytes.data()[index]);
        const bool safe = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                          (value >= '0' && value <= '9') || value == '-' || value == '_' || value == '.';
        if (safe) result += static_cast<wxChar>(value);
        else if (value == ' ') result += wxS("+");
        else result += wxString::Format(wxS("%%%c%c"), hex[value >> 4], hex[value & 0x0f]);
    }
    return result;
}

wxString CachePath(const wxString& registry, const wxString& query)
{
    wxString dataRoot;
    if (!wxGetEnv(wxS("CODIUM_BLOCKS_DATA"), &dataRoot) || dataRoot.empty()) {
        dataRoot = wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + wxS("CodiumBlocks");
    }
    const std::string keyInput = (registry + wxS("\n") + query).utf8_str().data();
    const size_t key = std::hash<std::string>{}(keyInput);
    std::ostringstream suffix;
    suffix << std::hex << key;
    const wxString directory = dataRoot + wxFILE_SEP_PATH + wxS("extension-cache");
    wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return directory + wxFILE_SEP_PATH + wxString::FromUTF8(suffix.str()) + wxS(".json");
}

bool WriteText(const wxString& path, const wxString& text, wxString* error)
{
    wxFile file;
    if (!file.Open(path, wxFile::write)) {
        if (error) *error = wxString::Format(wxS("Could not open catalog cache: %s."), path);
        return false;
    }
    const wxScopedCharBuffer bytes = text.utf8_str();
    if (file.Write(bytes.data(), bytes.length()) != bytes.length() || !file.Close()) {
        if (error) *error = wxString::Format(wxS("Could not write catalog cache: %s."), path);
        return false;
    }
    return true;
}

} // namespace

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

wxString ExtensionRegistry::OpenVsxSearchUrl(const wxString& registry, const wxString& query) const
{
    wxString base = registry;
    while (base.EndsWith(wxS("/"))) base.RemoveLast();
    if (!base.EndsWith(wxS("/api"))) base += wxS("/api");
    return base + wxS("/-/search?query=") + EncodeQuery(query);
}

bool ExtensionRegistry::CacheCatalog(const wxString& registry, const wxString& query, const wxString& json, wxString* error) const
{
    return WriteText(CachePath(registry, query), json, error);
}

bool ExtensionRegistry::LoadCachedCatalog(const wxString& registry, const wxString& query, wxString* json) const
{
    const wxString path = CachePath(registry, query);
    if (!wxFileExists(path)) return false;
    wxFile file;
    if (!file.Open(path, wxFile::read)) return false;
    return file.ReadAll(json);
}

bool ExtensionRegistry::SearchOpenVsx(const wxString& registry, const wxString& query, wxString* json, wxString* error) const
{
    if (!ExtensionSecurity::IsAllowedRegistryUrl(registry)) {
        if (error) *error = wxS("Open VSX search requires an HTTPS registry without credentials.");
        return false;
    }
    wxURL url(OpenVsxSearchUrl(registry, query));
    if (!url.IsOk()) {
        if (error) *error = wxS("Could not construct the Open VSX search URL.");
        return LoadCachedCatalog(registry, query, json);
    }
    std::unique_ptr<wxInputStream> input(url.GetInputStream());
    if (!input || !input->IsOk()) {
        if (LoadCachedCatalog(registry, query, json)) return true;
        if (error) *error = wxString::Format(wxS("Open VSX request failed: %s."), OpenVsxSearchUrl(registry, query));
        return false;
    }
    wxString response;
    wxStringOutputStream output(&response);
    input->Read(output);
    if (response.empty()) {
        if (LoadCachedCatalog(registry, query, json)) return true;
        if (error) *error = wxS("Open VSX returned an empty catalog response.");
        return false;
    }
    if (json) *json = response;
    wxString cacheError;
    CacheCatalog(registry, query, response, &cacheError);
    return true;
}

} // namespace codium
