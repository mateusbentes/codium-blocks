// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/dap_client.hpp"

#include <wx/defs.h>

#include <algorithm>
#include <cstddef>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const unsigned char* data, std::size_t size)
{
    constexpr std::size_t kMaximumInput = 1024U * 1024U;
    const std::size_t boundedSize = std::min(size, kMaximumInput);
    codium::DapClient client(nullptr, wxID_HIGHEST + 3100);
    if (boundedSize != 0U) {
        client.ParseBytesForTesting(std::string(reinterpret_cast<const char*>(data), boundedSize));
    }
    return 0;
}
