// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include <wx/string.h>

namespace codium {

class SignatureVerifier final {
public:
    static bool VerifyEd25519File(const wxString& path, const wxString& publicKeyHex,
                                  const wxString& signatureHex, wxString* error = nullptr);
};

} // namespace codium
