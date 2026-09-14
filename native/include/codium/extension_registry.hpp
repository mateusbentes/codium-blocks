// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include "codium/extension_security.hpp"

#include <wx/arrstr.h>
#include <wx/string.h>

#include <vector>

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

    wxString Identifier() const { return namespaceName + wxS(".") + name; }
};

class ExtensionRegistry final {
public:
    bool AddRegistry(const wxString& url, wxString* error = nullptr);
    bool RemoveRegistry(const wxString& url);
    const wxArrayString& Registries() const { return registries_; }
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
    wxArrayString registries_;
};

} // namespace codium
