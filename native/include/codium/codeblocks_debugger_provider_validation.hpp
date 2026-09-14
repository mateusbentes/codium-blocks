// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include "codium/codeblocks_debugger_provider.hpp"

#include <wx/string.h>

#include <cstdint>

namespace codium {

struct CodeBlocksDebuggerProviderExpectation final {
    std::uint32_t sdkMajor = 0;
    std::uint32_t sdkMinor = 0;
    std::uint32_t sdkRelease = 0;
    wxString sourceRevision;
    wxString abiIdentity;
};

inline bool ValidateCodeBlocksDebuggerProviderApi(const CodeBlocksDebuggerProviderApi* api,
                                                  const CodeBlocksDebuggerProviderExpectation& expected,
                                                  wxString* error = nullptr)
{
    const auto fail = [error](const wxString& message) {
        if (error) *error = message;
        return false;
    };
    if (!api) return fail(wxS("The DebuggerGDB provider returned a null API."));
    if (api->apiMajor != 1 || api->apiMinor != 0 || !api->attach || !api->detach ||
        !api->snapshot || !api->freeString) {
        return fail(wxS("The DebuggerGDB provider exposes an unsupported or incomplete ABI."));
    }
    if (api->sdkMajor != expected.sdkMajor || api->sdkMinor != expected.sdkMinor ||
        api->sdkRelease != expected.sdkRelease) {
        return fail(wxS("The DebuggerGDB provider SDK tuple does not match the adapter."));
    }
    if (!api->sourceRevision || wxString::FromUTF8(api->sourceRevision) != expected.sourceRevision) {
        return fail(wxS("The DebuggerGDB provider source revision does not match the adapter."));
    }
    if (!api->abiIdentity || wxString::FromUTF8(api->abiIdentity) != expected.abiIdentity) {
        return fail(wxS("The DebuggerGDB provider ABI identity does not match the adapter."));
    }
    if (!api->providerIdentity || wxString::FromUTF8(api->providerIdentity).empty()) {
        return fail(wxS("The DebuggerGDB provider does not expose a non-empty provider identity."));
    }
    return true;
}

} // namespace codium
