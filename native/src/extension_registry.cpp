// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/extension_registry.hpp"
#include "codium/signature_verifier.hpp"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/sstream.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/tokenzr.h>
#include <wx/url.h>
#include <wx/wfstream.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

namespace codium {

namespace {

wxString QuoteArgument(const wxString& value)
{
#if defined(__WXMSW__)
    wxString escaped = value;
    escaped.Replace(wxS("\\"), wxS("\\\\"));
    escaped.Replace(wxS("\""), wxS("\\\""));
    return wxS("\"") + escaped + wxS("\"");
#else
    wxString escaped = value;
    escaped.Replace(wxS("'"), wxS("'\\''"));
    return wxS("'") + escaped + wxS("'");
#endif
}

wxString CommandLine(const wxArrayString& arguments)
{
    wxString command;
    for (const auto& argument : arguments) {
        if (!command.empty()) command += wxS(" ");
        command += QuoteArgument(argument);
    }
    return command;
}

wxString EncodeQuery(const wxString& input)
{
    const wxScopedCharBuffer bytes = input.utf8_str();
    static const char hex[] = "0123456789ABCDEF";
    wxString result;
    for (size_t index = 0; index < bytes.length(); ++index) {
        const unsigned char value = static_cast<unsigned char>(bytes.data()[index]);
        const bool safe = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
                          (value >= '0' && value <= '9') || value == '-' || value == '_' ||
                          value == '.' || value == '~';
        if (safe) result += static_cast<wxChar>(value);
        else if (value == ' ') result += wxS("+");
        else result += wxString::Format(wxS("%%%c%c"), hex[value >> 4], hex[value & 0x0f]);
    }
    return result;
}

bool IsSafeSegment(const wxString& value)
{
    if (value.empty() || value == wxS(".") || value == wxS("..") || value.Contains(wxS(".."))) return false;
    for (const wxChar character : value) {
        const bool safe = (character >= wxChar('a') && character <= wxChar('z')) ||
                          (character >= wxChar('A') && character <= wxChar('Z')) ||
                          (character >= wxChar('0') && character <= wxChar('9')) ||
                          character == wxChar('-') || character == wxChar('_') || character == wxChar('.');
        if (!safe) return false;
    }
    return true;
}

bool IsHexSha256(const wxString& value)
{
    if (value.length() != 64) return false;
    for (const wxChar character : value) {
        const bool hex = (character >= wxChar('0') && character <= wxChar('9')) ||
                         (character >= wxChar('a') && character <= wxChar('f')) ||
                         (character >= wxChar('A') && character <= wxChar('F'));
        if (!hex) return false;
    }
    return true;
}

wxString CachePath(const wxString& registry, const wxString& query)
{
    wxString dataRoot;
    if (!wxGetEnv(wxS("CODIUM_BLOCKS_DATA"), &dataRoot) || dataRoot.empty()) {
        dataRoot = wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + wxS("CodiumBlocks");
    }
    const wxString keyInput = registry + wxS("\n") + query;
    const wxScopedCharBuffer bytes = keyInput.utf8_str();
    std::uint64_t hash = 1469598103934665603ULL;
    for (size_t index = 0; index < bytes.length(); ++index) {
        hash ^= static_cast<unsigned char>(bytes.data()[index]);
        hash *= 1099511628211ULL;
    }
    std::ostringstream suffix;
    suffix << std::hex << hash;
    const wxString directory = dataRoot + wxFILE_SEP_PATH + wxS("extension-cache");
    wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return directory + wxFILE_SEP_PATH + wxString::FromUTF8(suffix.str()) + wxS(".json");
}

wxString RegistryApiRoot(const wxString& registry)
{
    wxString base = registry;
    while (base.EndsWith(wxS("/"))) base.RemoveLast();
    if (!base.EndsWith(wxS("/api"))) base += wxS("/api");
    return base;
}

bool WriteText(const wxString& path, const wxString& text, wxString* error)
{
    wxFileName::Mkdir(wxFileName(path).GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const wxString temporary = path + wxS(".tmp");
    wxFile file;
    if (!file.Open(temporary, wxFile::write)) {
        if (error) *error = wxString::Format(wxS("Could not open catalog cache: %s."), temporary);
        return false;
    }
    const wxScopedCharBuffer bytes = text.utf8_str();
    const bool written = file.Write(bytes.data(), bytes.length()) == bytes.length() && file.Close();
    if (!written || !wxRenameFile(temporary, path, true)) {
        wxRemoveFile(temporary);
        if (error) *error = wxString::Format(wxS("Could not commit catalog cache: %s."), path);
        return false;
    }
    return true;
}

bool JsonString(const wxString& object, const wxString& field, wxString* value)
{
    const wxString marker = wxString::Format(wxS("\"%s\""), field);
    int search = 0;
    while (search < static_cast<int>(object.length())) {
        const int relative = object.Mid(search).Find(marker);
        if (relative == wxNOT_FOUND) break;
        search += relative;
        int cursor = search + static_cast<int>(marker.length());
        while (cursor < static_cast<int>(object.length()) &&
               (object[cursor] == wxChar(' ') || object[cursor] == wxChar('\t') ||
                object[cursor] == wxChar('\r') || object[cursor] == wxChar('\n'))) ++cursor;
        if (cursor >= static_cast<int>(object.length()) || object[cursor] != wxChar(':')) {
            search += static_cast<int>(marker.length());
            continue;
        }
        ++cursor;
        while (cursor < static_cast<int>(object.length()) &&
               (object[cursor] == wxChar(' ') || object[cursor] == wxChar('\t') ||
                object[cursor] == wxChar('\r') || object[cursor] == wxChar('\n'))) ++cursor;
        if (cursor >= static_cast<int>(object.length()) || object[cursor] != wxChar('"')) {
            search += static_cast<int>(marker.length());
            continue;
        }
        ++cursor;
        wxString result;
        bool escaped = false;
        for (; cursor < static_cast<int>(object.length()); ++cursor) {
            const wxChar character = object[cursor];
            if (!escaped && character == wxChar('"')) {
                if (value) *value = result;
                return true;
            }
            if (escaped) {
                switch (character) {
                case wxChar('n'): result += wxChar('\n'); break;
                case wxChar('r'): result += wxChar('\r'); break;
                case wxChar('t'): result += wxChar('\t'); break;
                case wxChar('b'): result += wxChar('\b'); break;
                case wxChar('f'): result += wxChar('\f'); break;
                default: result += character; break;
                }
                escaped = false;
            } else if (character == wxChar('\\')) {
                escaped = true;
            } else {
                result += character;
            }
        }
        return false;
    }
    return false;
}

class JsonValidator final {
public:
    explicit JsonValidator(const wxString& text) : text_(text) {}

    bool ParseDocument()
    {
        SkipWhitespace();
        if (!ParseValue(0)) return false;
        SkipWhitespace();
        return position_ == text_.length();
    }

private:
    static bool IsHex(wxChar character)
    {
        return (character >= wxChar('0') && character <= wxChar('9')) ||
               (character >= wxChar('a') && character <= wxChar('f')) ||
               (character >= wxChar('A') && character <= wxChar('F'));
    }

    void SkipWhitespace()
    {
        while (position_ < text_.length() &&
               (text_[position_] == wxChar(' ') || text_[position_] == wxChar('\t') ||
                text_[position_] == wxChar('\r') || text_[position_] == wxChar('\n'))) {
            ++position_;
        }
    }

    bool ParseString()
    {
        if (position_ >= text_.length() || text_[position_] != wxChar('"')) return false;
        ++position_;
        while (position_ < text_.length()) {
            const wxChar character = text_[position_++];
            if (character == wxChar('"')) return true;
            if (character < wxChar(0x20)) return false;
            if (character == wxChar('\\')) {
                if (position_ >= text_.length()) return false;
                const wxChar escaped = text_[position_++];
                if (escaped == wxChar('u')) {
                    if (position_ + 4 > text_.length()) return false;
                    for (size_t index = 0; index < 4; ++index) {
                        if (!IsHex(text_[position_++])) return false;
                    }
                } else if (escaped != wxChar('"') && escaped != wxChar('\\') && escaped != wxChar('/') &&
                           escaped != wxChar('b') && escaped != wxChar('f') && escaped != wxChar('n') &&
                           escaped != wxChar('r') && escaped != wxChar('t')) {
                    return false;
                }
            }
        }
        return false;
    }

    bool ParseNumber()
    {
        const size_t start = position_;
        if (position_ < text_.length() && text_[position_] == wxChar('-')) ++position_;
        if (position_ >= text_.length()) return false;
        if (text_[position_] == wxChar('0')) {
            ++position_;
        } else {
            if (text_[position_] < wxChar('1') || text_[position_] > wxChar('9')) return false;
            while (position_ < text_.length() && text_[position_] >= wxChar('0') && text_[position_] <= wxChar('9')) ++position_;
        }
        if (position_ < text_.length() && text_[position_] == wxChar('.')) {
            ++position_;
            const size_t fractionStart = position_;
            while (position_ < text_.length() && text_[position_] >= wxChar('0') && text_[position_] <= wxChar('9')) ++position_;
            if (position_ == fractionStart) return false;
        }
        if (position_ < text_.length() && (text_[position_] == wxChar('e') || text_[position_] == wxChar('E'))) {
            ++position_;
            if (position_ < text_.length() && (text_[position_] == wxChar('+') || text_[position_] == wxChar('-'))) ++position_;
            const size_t exponentStart = position_;
            while (position_ < text_.length() && text_[position_] >= wxChar('0') && text_[position_] <= wxChar('9')) ++position_;
            if (position_ == exponentStart) return false;
        }
        return position_ > start;
    }

    bool ParseValue(size_t depth)
    {
        if (depth > 64 || position_ >= text_.length()) return false;
        switch (static_cast<wchar_t>(text_[position_])) {
        case wxChar('{'): return ParseObject(depth + 1);
        case wxChar('['): return ParseArray(depth + 1);
        case wxChar('"'): return ParseString();
        case wxChar('t'): return ParseLiteral(wxS("true"));
        case wxChar('f'): return ParseLiteral(wxS("false"));
        case wxChar('n'): return ParseLiteral(wxS("null"));
        default: return ParseNumber();
        }
    }

    bool ParseLiteral(const wxString& literal)
    {
        if (text_.Mid(position_, literal.length()) != literal) return false;
        position_ += literal.length();
        return true;
    }

    bool ParseObject(size_t depth)
    {
        ++position_;
        SkipWhitespace();
        if (position_ < text_.length() && text_[position_] == wxChar('}')) {
            ++position_;
            return true;
        }
        while (position_ < text_.length()) {
            if (!ParseString()) return false;
            SkipWhitespace();
            if (position_ >= text_.length() || text_[position_++] != wxChar(':')) return false;
            SkipWhitespace();
            if (!ParseValue(depth)) return false;
            SkipWhitespace();
            if (position_ < text_.length() && text_[position_] == wxChar('}')) {
                ++position_;
                return true;
            }
            if (position_ >= text_.length() || text_[position_++] != wxChar(',')) return false;
            SkipWhitespace();
        }
        return false;
    }

    bool ParseArray(size_t depth)
    {
        ++position_;
        SkipWhitespace();
        if (position_ < text_.length() && text_[position_] == wxChar(']')) {
            ++position_;
            return true;
        }
        while (position_ < text_.length()) {
            if (!ParseValue(depth)) return false;
            SkipWhitespace();
            if (position_ < text_.length() && text_[position_] == wxChar(']')) {
                ++position_;
                return true;
            }
            if (position_ >= text_.length() || text_[position_++] != wxChar(',')) return false;
            SkipWhitespace();
        }
        return false;
    }

    const wxString& text_;
    size_t position_ = 0;
};

std::vector<wxString> JsonObjectsInExtensions(const wxString& json)
{
    std::vector<wxString> result;
    const int extensions = json.Find(wxS("\"extensions\""));
    if (extensions == wxNOT_FOUND) return result;
    const int relativeArrayStart = json.Mid(extensions).Find(wxChar('['));
    const int arrayStart = relativeArrayStart == wxNOT_FOUND ? wxNOT_FOUND : extensions + relativeArrayStart;
    if (arrayStart == wxNOT_FOUND) return result;
    int depth = 0;
    int objectStart = wxNOT_FOUND;
    bool quoted = false;
    bool escaped = false;
    for (int index = arrayStart + 1; index < static_cast<int>(json.length()); ++index) {
        const wxChar character = json[index];
        if (quoted) {
            if (escaped) escaped = false;
            else if (character == wxChar('\\')) escaped = true;
            else if (character == wxChar('"')) quoted = false;
            continue;
        }
        if (character == wxChar('"')) {
            quoted = true;
        } else if (character == wxChar('{')) {
            if (depth == 0) objectStart = index;
            ++depth;
        } else if (character == wxChar('}')) {
            if (depth > 0) --depth;
            if (depth == 0 && objectStart != wxNOT_FOUND) {
                result.push_back(json.Mid(objectStart, index - objectStart + 1));
                objectStart = wxNOT_FOUND;
            }
        } else if (character == wxChar(']') && depth == 0) {
            break;
        }
    }
    return result;
}

bool ParseEntry(const wxString& object, const wxString& registry, ExtensionCatalogEntry* entry)
{
    ExtensionCatalogEntry candidate;
    candidate.registry = registry;
    if (!JsonString(object, wxS("namespace"), &candidate.namespaceName) ||
        !JsonString(object, wxS("name"), &candidate.name) ||
        !JsonString(object, wxS("version"), &candidate.version) ||
        !IsSafeSegment(candidate.namespaceName) || !IsSafeSegment(candidate.name) ||
        !IsSafeSegment(candidate.version)) {
        return false;
    }
    JsonString(object, wxS("displayName"), &candidate.displayName);
    JsonString(object, wxS("description"), &candidate.description);
    JsonString(object, wxS("download"), &candidate.downloadUrl);
    if (candidate.downloadUrl.empty()) JsonString(object, wxS("downloadUrl"), &candidate.downloadUrl);
    JsonString(object, wxS("url"), &candidate.webUrl);
    wxString publishedSha256;
    JsonString(object, wxS("sha256"), &publishedSha256);
    if (publishedSha256.empty()) JsonString(object, wxS("sha256sum"), &publishedSha256);
    if (IsHexSha256(publishedSha256)) candidate.sha256 = publishedSha256;
    else if (!publishedSha256.empty()) candidate.sha256Url = publishedSha256;
    JsonString(object, wxS("signature"), &candidate.signatureHex);
    if (candidate.signatureHex.empty()) JsonString(object, wxS("signatureHex"), &candidate.signatureHex);
    JsonString(object, wxS("signatureUrl"), &candidate.signatureUrl);
    if (entry) *entry = candidate;
    return true;
}

bool FetchText(const wxString& url, wxString* text, wxString* error,
               const wxString& bearerToken = wxEmptyString)
{
    wxURL remote(url);
    if (!remote.IsOk()) {
        if (error) *error = wxString::Format(wxS("Could not construct HTTPS URL: %s."), url);
        return false;
    }
    if (!bearerToken.empty()) {
        wxArrayString arguments;
        arguments.Add(wxS("curl"));
        arguments.Add(wxS("--fail-with-body"));
        arguments.Add(wxS("--silent"));
        arguments.Add(wxS("--show-error"));
        arguments.Add(wxS("--location"));
        arguments.Add(wxS("--max-time"));
        arguments.Add(wxS("30"));
        arguments.Add(wxS("--proto"));
        arguments.Add(wxS("=https"));
        arguments.Add(wxS("--header"));
        arguments.Add(wxS("Authorization: Bearer ") + bearerToken);
        arguments.Add(url);
        wxArrayString output;
        wxArrayString errors;
        const long exitCode = wxExecute(CommandLine(arguments), output, errors, wxEXEC_SYNC);
        if (exitCode != 0) {
            if (error) *error = wxString::Format(wxS("Authenticated HTTPS request failed for %s (curl exit %ld)."), url, exitCode);
            return false;
        }
        text->clear();
        for (const wxString& line : output) *text += line + wxS("\n");
        return true;
    }
    std::unique_ptr<wxInputStream> input(remote.GetInputStream());
    if (!input || !input->IsOk()) {
        if (error) *error = wxString::Format(wxS("HTTPS request failed: %s."), url);
        return false;
    }
    wxString response;
    wxStringOutputStream output(&response);
    while (input->CanRead()) {
        input->Read(output);
        if (input->LastRead() == 0) break;
        if (response.length() > 32 * 1024 * 1024) {
            if (error) *error = wxS("Registry response exceeds the 32 MiB safety limit.");
            return false;
        }
    }
    if (response.empty()) {
        if (error) *error = wxS("Registry returned an empty response.");
        return false;
    }
    if (text) *text = response;
    return true;
}

bool ExtractSha256(const wxString& text, wxString* digest)
{
    for (size_t index = 0; index + 64 <= text.length(); ++index) {
        const wxString candidate = text.Mid(index, 64);
        if (IsHexSha256(candidate)) {
            if (digest) *digest = candidate.Lower();
            return true;
        }
    }
    return false;
}

bool ExtractHex(const wxString& text, size_t length, wxString* value)
{
    const auto isHex = [](wxChar character) {
        return (character >= wxChar('0') && character <= wxChar('9')) ||
               (character >= wxChar('a') && character <= wxChar('f')) ||
               (character >= wxChar('A') && character <= wxChar('F'));
    };
    for (size_t index = 0; index + length <= text.length(); ++index) {
        const wxString candidate = text.Mid(index, length);
        bool valid = true;
        for (size_t offset = 0; offset < candidate.length(); ++offset) {
            if (!isHex(candidate[offset])) { valid = false; break; }
        }
        if (valid) {
            if (value) *value = candidate;
            return true;
        }
    }
    return false;
}

bool AllowedRegistryArtifactUrl(const wxString& registry, const wxString& url)
{
    const wxString prefix = RegistryApiRoot(registry) + wxS("/");
    return url.StartsWith(wxS("https://")) && url.StartsWith(prefix) && !url.Contains(wxS("@")) &&
           !url.Contains(wxS("\\")) && !url.Contains(wxS("/../"));
}

wxString ConstructDownloadUrl(const wxString& registry, const ExtensionCatalogEntry& entry)
{
    if (!entry.downloadUrl.empty()) return entry.downloadUrl;
    return RegistryApiRoot(registry) + wxS("/") + entry.namespaceName + wxS("/") + entry.name +
           wxS("/") + entry.version + wxS("/file/") + entry.namespaceName + wxS(".") + entry.name +
           wxS("-") + entry.version + wxS(".vsix");
}

} // namespace

ExtensionRegistry::ExtensionRegistry()
{
    registries_.Add(wxS("https://open-vsx.org"));
}

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
    if (registries_.size() <= 1) return false;
    const int index = registries_.Index(url);
    if (index == wxNOT_FOUND) return false;
    registries_.RemoveAt(static_cast<size_t>(index));
    return true;
}

bool ExtensionRegistry::SetBearerToken(const wxString& registry, const wxString& token, wxString* error)
{
    if (!ExtensionSecurity::IsAllowedRegistryUrl(registry) || token.empty() ||
        token.Contains(wxChar('\r')) || token.Contains(wxChar('\n'))) {
        if (error) *error = wxS("A private registry requires a non-empty token without line breaks.");
        return false;
    }
    if (!IsConfiguredRegistry(registry) && !AddRegistry(registry, error)) return false;
    bearerTokens_[registry] = token;
    return true;
}

bool ExtensionRegistry::SetPolicy(const wxString& registry, const ExtensionRegistryPolicy& policy,
                                   wxString* error)
{
    if (!ExtensionSecurity::IsAllowedRegistryUrl(registry) ||
        (policy.requireEd25519Signature && !IsHexSha256(policy.publicKeyHex))) {
        if (error) *error = wxS("The registry policy is invalid or lacks the Ed25519 public key.");
        return false;
    }
    if (!IsConfiguredRegistry(registry) && !AddRegistry(registry, error)) return false;
    policies_[registry] = policy;
    return true;
}

ExtensionRegistryPolicy ExtensionRegistry::Policy(const wxString& registry) const
{
    const auto found = policies_.find(registry);
    return found == policies_.end() ? ExtensionRegistryPolicy{} : found->second;
}

bool ExtensionRegistry::CanRunUnattendedUpdates(const wxString& registry) const
{
    return IsConfiguredRegistry(registry) && Policy(registry).allowUnattendedUpdates;
}

bool ExtensionRegistry::IsConfiguredRegistry(const wxString& registry) const
{
    return registries_.IsEmpty() || registries_.Index(registry) != wxNOT_FOUND;
}

wxString ExtensionRegistry::AuthorizationToken(const wxString& registry) const
{
    const auto found = bearerTokens_.find(registry);
    if (found != bearerTokens_.end()) return found->second;
    wxString environmentToken;
    if (wxGetEnv(wxS("CODIUM_BLOCKS_REGISTRY_TOKEN"), &environmentToken)) return environmentToken;
    return wxString(wxEmptyString);
}

bool ExtensionRegistry::VerifyArtifact(const wxString& path, const wxString& expectedSha256, wxString* error) const
{
    if (expectedSha256.length() != 64) {
        if (error) *error = wxS("An expected 64-character SHA-256 digest is required for registry artifacts.");
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
    return RegistryApiRoot(registry) + wxS("/-/search?query=") + EncodeQuery(query);
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
    if (!ExtensionSecurity::IsAllowedRegistryUrl(registry) || !IsConfiguredRegistry(registry)) {
        if (error) *error = wxS("Open VSX search requires an HTTPS registry without credentials.");
        return false;
    }
    const wxString url = OpenVsxSearchUrl(registry, query);
    wxString response;
    if (!FetchText(url, &response, error, AuthorizationToken(registry))) {
        if (LoadCachedCatalog(registry, query, json)) return true;
        if (error && error->empty()) *error = wxString::Format(wxS("Open VSX request failed: %s."), url);
        return false;
    }
    if (json) *json = response;
    wxString cacheError;
    CacheCatalog(registry, query, response, &cacheError);
    return true;
}

bool ExtensionRegistry::FetchOpenVsxMetadata(const wxString& registry, const wxString& namespaceName,
                                             const wxString& name, const wxString& version,
                                             ExtensionCatalogEntry* entry, wxString* error) const
{
    if (!ExtensionSecurity::IsAllowedRegistryUrl(registry) || !IsConfiguredRegistry(registry) ||
        !IsSafeSegment(namespaceName) ||
        !IsSafeSegment(name) || !IsSafeSegment(version)) {
        if (error) *error = wxS("Invalid registry or extension identifier.");
        return false;
    }
    const wxString url = RegistryApiRoot(registry) + wxS("/") + namespaceName + wxS("/") + name +
                         (version.empty() ? wxString() : wxString(wxS("/")) + version);
    wxString response;
    if (!FetchText(url, &response, error, AuthorizationToken(registry))) return false;
    if (!JsonValidator(response).ParseDocument()) {
        if (error) *error = wxS("Open VSX metadata is not valid JSON.");
        return false;
    }
    ExtensionCatalogEntry candidate;
    if (!ParseEntry(response, registry, &candidate)) {
        if (error) *error = wxS("Open VSX metadata did not contain a safe namespace, name, and version.");
        return false;
    }
    if (entry) *entry = candidate;
    return true;
}

bool ExtensionRegistry::IsNewerVersion(const wxString& candidate, const wxString& installed)
{
    wxStringTokenizer candidateParts(candidate, wxS("."));
    wxStringTokenizer installedParts(installed, wxS("."));
    while (candidateParts.HasMoreTokens() || installedParts.HasMoreTokens()) {
        long candidateValue = 0;
        long installedValue = 0;
        if (candidateParts.HasMoreTokens()) {
            const wxString token = candidateParts.GetNextToken();
            if (!token.ToLong(&candidateValue)) return false;
        }
        if (installedParts.HasMoreTokens()) {
            const wxString token = installedParts.GetNextToken();
            if (!token.ToLong(&installedValue)) return false;
        }
        if (candidateValue != installedValue) return candidateValue > installedValue;
    }
    return false;
}

bool ExtensionRegistry::ParseOpenVsxCatalog(const wxString& json, std::vector<ExtensionCatalogEntry>* entries,
                                            wxString* error) const
{
    if (!entries) return false;
    entries->clear();
    if (json.length() > 32 * 1024 * 1024) {
        if (error) *error = wxS("Open VSX catalog exceeds the 32 MiB safety limit.");
        return false;
    }
    if (!JsonValidator(json).ParseDocument()) {
        if (error) *error = wxS("Open VSX catalog is not valid JSON.");
        return false;
    }
    const auto objects = JsonObjectsInExtensions(json);
    if (json.Find(wxS("\"extensions\"")) == wxNOT_FOUND) {
        if (error) *error = wxS("Open VSX response does not contain an extensions array.");
        return false;
    }
    for (const auto& object : objects) {
        ExtensionCatalogEntry entry;
        if (ParseEntry(object, wxEmptyString, &entry)) entries->push_back(entry);
    }
    if (!objects.empty() && entries->empty()) {
        if (error) *error = wxS("Open VSX catalog contained no valid extension entries.");
        return false;
    }
    return true;
}

bool ExtensionRegistry::DownloadArtifact(const wxString& registry, const ExtensionCatalogEntry& entry,
                                         const wxString& destination, wxString* digest, wxString* error) const
{
    if (!ExtensionSecurity::IsAllowedRegistryUrl(registry) || !IsConfiguredRegistry(registry) ||
        !IsSafeSegment(entry.namespaceName) ||
        !IsSafeSegment(entry.name) || !IsSafeSegment(entry.version) || destination.empty()) {
        if (error) *error = wxS("Invalid registry extension metadata or destination.");
        return false;
    }
    const wxString url = ConstructDownloadUrl(registry, entry);
    if (!AllowedRegistryArtifactUrl(registry, url)) {
        if (error) *error = wxS("The registry returned a download URL outside its HTTPS API origin.");
        return false;
    }
    const wxString parent = wxFileName(destination).GetPath();
    wxFileName::Mkdir(parent, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const wxString temporary = destination + wxS(".download");
    wxRemoveFile(temporary);
    constexpr wxFileOffset kMaximumDownloadBytes = 512LL * 1024LL * 1024LL;
    wxFileOffset total = 0;
    const wxString bearerToken = AuthorizationToken(registry);
    if (!bearerToken.empty()) {
        wxArrayString arguments;
        arguments.Add(wxS("curl"));
        arguments.Add(wxS("--fail"));
        arguments.Add(wxS("--silent"));
        arguments.Add(wxS("--show-error"));
        arguments.Add(wxS("--location"));
        arguments.Add(wxS("--max-time"));
        arguments.Add(wxS("120"));
        arguments.Add(wxS("--proto"));
        arguments.Add(wxS("=https"));
        arguments.Add(wxS("--header"));
        arguments.Add(wxS("Authorization: Bearer ") + bearerToken);
        arguments.Add(wxS("--output"));
        arguments.Add(temporary);
        arguments.Add(url);
        wxArrayString output;
        wxArrayString errors;
        if (wxExecute(CommandLine(arguments), output, errors, wxEXEC_SYNC) != 0) {
            wxRemoveFile(temporary);
            if (error) *error = wxString::Format(wxS("Authenticated VSIX download failed for %s."), url);
            return false;
        }
        wxFile downloaded;
        if (!downloaded.Open(temporary, wxFile::read)) {
            wxRemoveFile(temporary);
            if (error) *error = wxS("Could not inspect the authenticated VSIX download.");
            return false;
        }
        total = downloaded.Length();
        downloaded.Close();
    } else {
        wxURL remote(url);
        if (!remote.IsOk()) {
            if (error) *error = wxString::Format(wxS("Could not construct VSIX URL: %s."), url);
            return false;
        }
        std::unique_ptr<wxInputStream> input(remote.GetInputStream());
        if (!input || !input->IsOk()) {
            if (error) *error = wxString::Format(wxS("VSIX download failed: %s."), url);
            return false;
        }
        wxFFileOutputStream output(temporary);
        if (!output.IsOk()) {
            if (error) *error = wxString::Format(wxS("Could not create VSIX download: %s."), temporary);
            return false;
        }
        char buffer[64 * 1024];
        while (input->CanRead()) {
            input->Read(buffer, sizeof(buffer));
            const size_t count = input->LastRead();
            if (count == 0) break;
            if (total > kMaximumDownloadBytes - static_cast<wxFileOffset>(count)) {
                output.Close();
                wxRemoveFile(temporary);
                if (error) *error = wxS("VSIX download exceeds the 512 MiB safety limit.");
                return false;
            }
            output.Write(buffer, count);
            if (!output.IsOk() || output.LastWrite() != count) {
                output.Close();
                wxRemoveFile(temporary);
                if (error) *error = wxS("Could not write the downloaded VSIX.");
                return false;
            }
            total += static_cast<wxFileOffset>(count);
        }
        output.Close();
    }
    if (total < 0 || total > kMaximumDownloadBytes) {
        wxRemoveFile(temporary);
        if (error) *error = wxS("VSIX download exceeds the 512 MiB safety limit.");
        return false;
    }
    if (total == 0 || !wxRenameFile(temporary, destination, true)) {
        wxRemoveFile(temporary);
        if (error) *error = wxS("Could not commit the downloaded VSIX atomically.");
        return false;
    }
    if (!ExtensionSecurity::ComputeSha256(destination, digest, error)) {
        wxRemoveFile(destination);
        return false;
    }
    wxString expectedSha256 = entry.sha256;
    if (expectedSha256.empty() && !entry.sha256Url.empty()) {
        if (!AllowedRegistryArtifactUrl(registry, entry.sha256Url)) {
            wxRemoveFile(destination);
            if (error) *error = wxS("The registry returned a checksum URL outside its HTTPS API origin.");
            return false;
        }
        wxString checksumText;
        if (!FetchText(entry.sha256Url, &checksumText, error, AuthorizationToken(registry)) ||
            !ExtractSha256(checksumText, &expectedSha256)) {
            wxRemoveFile(destination);
            if (error && error->empty()) *error = wxS("The registry checksum response did not contain a SHA-256 digest.");
            return false;
        }
    }
    if (!expectedSha256.empty() && !VerifyArtifact(destination, expectedSha256, error)) {
        wxRemoveFile(destination);
        return false;
    }
    const ExtensionRegistryPolicy policy = Policy(registry);
    if (policy.requirePublishedSha256 && expectedSha256.empty()) {
        wxRemoveFile(destination);
        if (error) *error = wxS("The registry policy requires a published SHA-256 digest.");
        return false;
    }
    if (policy.requireEd25519Signature) {
        wxString signature = entry.signatureHex;
        if (signature.empty() && !entry.signatureUrl.empty()) {
            if (!AllowedRegistryArtifactUrl(registry, entry.signatureUrl)) {
                wxRemoveFile(destination);
                if (error) *error = wxS("The registry returned a signature URL outside its HTTPS API origin.");
                return false;
            }
            wxString signatureText;
            if (!FetchText(entry.signatureUrl, &signatureText, error, AuthorizationToken(registry))) {
                wxRemoveFile(destination);
                return false;
            }
            if (!ExtractHex(signatureText, 128, &signature)) {
                wxRemoveFile(destination);
                if (error) *error = wxS("The registry signature response did not contain a 64-byte Ed25519 signature.");
                return false;
            }
        }
        if (signature.empty() || !SignatureVerifier::VerifyEd25519File(destination, policy.publicKeyHex, signature, error)) {
            wxRemoveFile(destination);
            if (error && error->empty()) *error = wxS("The registry artifact signature could not be verified.");
            return false;
        }
    }
    return true;
}

} // namespace codium
