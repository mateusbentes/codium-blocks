// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/codeblocks_debugger_provider_validation.hpp"

#include <cstdlib>
#include <iostream>

namespace {

bool Attach(void*, std::uint32_t, std::uint32_t, std::uint32_t, const char*, const char*, char**)
{
    return true;
}

void Detach() {}

bool Snapshot(const char*, const char*, char**, char**)
{
    return true;
}

void FreeString(char* value)
{
    std::free(value);
}

} // namespace

int main()
{
    const codium::CodeBlocksDebuggerProviderApi api{
        1, 0, 2, 23, 0, "source-r1", "abi-r1", "provider-r1",
        &Attach, &Detach, &Snapshot, &FreeString};
    const codium::CodeBlocksDebuggerProviderExpectation expected{
        2, 23, 0, wxS("source-r1"), wxS("abi-r1")};
    wxString error;
    if (!codium::ValidateCodeBlocksDebuggerProviderApi(&api, expected, &error)) {
        std::cerr << "provider-contract-smoke: valid API rejected: " << error.ToStdString() << "\n";
        return 1;
    }

    auto mismatch = api;
    mismatch.sdkMinor = 22;
    if (codium::ValidateCodeBlocksDebuggerProviderApi(&mismatch, expected, &error)) {
        std::cerr << "provider-contract-smoke: SDK mismatch accepted\n";
        return 2;
    }

    mismatch = api;
    mismatch.providerIdentity = "";
    if (codium::ValidateCodeBlocksDebuggerProviderApi(&mismatch, expected, &error)) {
        std::cerr << "provider-contract-smoke: empty provider identity accepted\n";
        return 3;
    }

    mismatch = api;
    mismatch.snapshot = nullptr;
    if (codium::ValidateCodeBlocksDebuggerProviderApi(&mismatch, expected, &error)) {
        std::cerr << "provider-contract-smoke: incomplete callback table accepted\n";
        return 4;
    }

    std::cout << "provider-contract-smoke: ok — ABI, SDK tuple, identity, and callback validation\n";
    return 0;
}
