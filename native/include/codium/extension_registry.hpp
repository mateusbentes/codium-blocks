// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

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
    wxString OpenVsxSearchUrl(const wxString& registry, const wxString& query) const;
    bool SearchOpenVsx(const wxString& registry, const wxString& query, wxString* json, wxString* error = nullptr) const;
    bool CacheCatalog(const wxString& registry, const wxString& query, const wxString& json, wxString* error = nullptr) const;
    bool LoadCachedCatalog(const wxString& registry, const wxString& query, wxString* json) const;

private:
    wxArrayString registries_;
};

} // namespace codium
