// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include "codium/extension_security.hpp"

#include <wx/arrstr.h>
#include <wx/string.h>

#include <vector>
#include <map>

namespace codium {

struct ExtensionCatalogEntry final {
    wxString registry;
    wxString namespaceName;
    wxString name;
    wxString version;
    wxString displayName;
    wxString description;
    wxString downloadUrl;
    wxString webUrl;
    wxString sha256;
    wxString sha256Url;
    wxString signatureHex;
    wxString signatureUrl;

    wxString Identifier() const { return namespaceName + wxS(".") + name; }
};

struct ExtensionRegistryPolicy final {
    bool allowUnattendedUpdates = false;
    bool requirePublishedSha256 = false;
    bool requireEd25519Signature = false;
    wxString publicKeyHex;
};

class ExtensionRegistry final {
public:
    ExtensionRegistry();
    bool AddRegistry(const wxString& url, wxString* error = nullptr);
    bool RemoveRegistry(const wxString& url);
    const wxArrayString& Registries() const { return registries_; }
    bool SetBearerToken(const wxString& registry, const wxString& token, wxString* error = nullptr);
    bool SetPolicy(const wxString& registry, const ExtensionRegistryPolicy& policy, wxString* error = nullptr);
    ExtensionRegistryPolicy Policy(const wxString& registry) const;
    bool CanRunUnattendedUpdates(const wxString& registry) const;
    bool VerifyArtifact(const wxString& path, const wxString& expectedSha256, wxString* error = nullptr) const;

    wxString OpenVsxSearchUrl(const wxString& registry, const wxString& query) const;
    bool SearchOpenVsx(const wxString& registry, const wxString& query, wxString* json,
                      wxString* error = nullptr) const;
    bool FetchOpenVsxMetadata(const wxString& registry, const wxString& namespaceName,
                              const wxString& name, const wxString& version,
                              ExtensionCatalogEntry* entry, wxString* error = nullptr) const;
    bool ParseOpenVsxCatalog(const wxString& json, std::vector<ExtensionCatalogEntry>* entries,
                             wxString* error = nullptr) const;
    bool DownloadArtifact(const wxString& registry, const ExtensionCatalogEntry& entry,
                          const wxString& destination, wxString* digest,
                          wxString* error = nullptr) const;
    static bool IsNewerVersion(const wxString& candidate, const wxString& installed);

    bool CacheCatalog(const wxString& registry, const wxString& query, const wxString& json,
                      wxString* error = nullptr) const;
    bool LoadCachedCatalog(const wxString& registry, const wxString& query, wxString* json) const;

private:
    bool IsConfiguredRegistry(const wxString& registry) const;
    wxString AuthorizationToken(const wxString& registry) const;
    wxArrayString registries_;
    std::map<wxString, wxString> bearerTokens_;
    std::map<wxString, ExtensionRegistryPolicy> policies_;
};

} // namespace codium
