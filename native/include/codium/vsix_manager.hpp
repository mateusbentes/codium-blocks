// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include "codium/extension_security.hpp"

#include <wx/arrstr.h>
#include <wx/string.h>

#include <vector>

namespace codium {

struct InstalledExtensionInfo final {
    ExtensionManifest manifest;
    wxString directory;
    wxString digest;
    bool trusted = false;
};

class VsixManager final {
public:
    explicit VsixManager(wxString extensionRoot);

    // Deliberately rejects unverified packages; use InstallVerified or InstallSigned.
    bool Install(const wxString& vsixPath, wxString* message = nullptr);
    bool InstallVerified(const wxString& vsixPath, const wxString& expectedSha256,
                         wxString* message = nullptr);
    bool InstallSigned(const wxString& vsixPath, const wxString& expectedSha256,
                       const wxString& publicKeyHex, const wxString& signatureHex,
                       wxString* message = nullptr);
    wxArrayString ListInstalled() const;
    std::vector<InstalledExtensionInfo> ListInstalledInfo() const;
    const wxString& ExtensionRoot() const { return extensionRoot_; }

private:
    bool InstallVerifiedInternal(const wxString& vsixPath, const wxString& expectedSha256,
                                 bool trusted, wxString* message);
    wxString extensionRoot_;
};

} // namespace codium
