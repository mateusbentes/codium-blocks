// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#pragma once

#include <cstdint>

namespace codium {

/**
 * Stable, opaque boundary between the generic Code::Blocks adapter and the
 * separately compiled DebuggerGDB provider. No Code::Blocks private type is
 * present in this header.
 */
struct CodeBlocksDebuggerProviderApi final {
    std::uint32_t apiMajor = 0;
    std::uint32_t apiMinor = 0;
    std::uint32_t sdkMajor = 0;
    std::uint32_t sdkMinor = 0;
    std::uint32_t sdkRelease = 0;
    const char* sourceRevision = nullptr;
    const char* abiIdentity = nullptr;
    const char* providerIdentity = nullptr;

    bool (*attach)(void* opaqueDebuggerPlugin,
                   std::uint32_t expectedSdkMajor,
                   std::uint32_t expectedSdkMinor,
                   std::uint32_t expectedSdkRelease,
                   const char* expectedSourceRevision,
                   const char* expectedAbiIdentity,
                   char** error) = nullptr;
    void (*detach)() = nullptr;
    bool (*snapshot)(const char* dataKind,
                    const char* expression,
                    char** json,
                    char** error) = nullptr;
    void (*freeString)(char*) = nullptr;
};

using CodeBlocksDebuggerProviderGetApi = const CodeBlocksDebuggerProviderApi* (*)();

} // namespace codium

extern "C" {
/** Exported by the separately compiled DebuggerGDB provider shared library. */
const codium::CodeBlocksDebuggerProviderApi* codium_blocks_debugger_provider_get_api();
}

#ifndef CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION
#define CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION "unspecified"
#endif

#ifndef CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_IDENTITY
#define CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_IDENTITY "unspecified"
#endif

#ifndef CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_PROVIDER_IDENTITY
#define CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_PROVIDER_IDENTITY "unspecified"
#endif
#ifndef CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MAJOR
#define CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MAJOR 0
#endif
#ifndef CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MINOR
#define CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MINOR 0
#endif
#ifndef CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_RELEASE
#define CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_RELEASE 0
#endif
#ifndef CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_PRIVATE_PROVIDER_ENABLED
#define CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_PRIVATE_PROVIDER_ENABLED 0
#endif

static_assert(sizeof(void*) >= 4, "The provider ABI requires a normal pointer-sized platform.");

extern "C" {
using CodiumBlocksDebuggerProviderGetApi = codium::CodeBlocksDebuggerProviderGetApi;
}

#if defined(CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_PROVIDER_BUILD)
#if defined(_WIN32)
#define CODIUM_BLOCKS_DEBUGGER_PROVIDER_EXPORT __declspec(dllexport)
#else
#define CODIUM_BLOCKS_DEBUGGER_PROVIDER_EXPORT __attribute__((visibility("default")))
#endif
extern "C" CODIUM_BLOCKS_DEBUGGER_PROVIDER_EXPORT
const codium::CodeBlocksDebuggerProviderApi* codium_blocks_debugger_provider_get_api();
#endif
