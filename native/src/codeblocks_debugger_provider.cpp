// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/codeblocks_debugger_provider.hpp"

#include <wx/string.h>

#include <cbplugin.h>
#include <debuggermanager.h>

#include "debuggergdb.h"
#include "debugger_defs.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

namespace {

constexpr std::uint32_t kApiMajor = 1;
constexpr std::uint32_t kApiMinor = 0;
constexpr std::size_t kMaxItems = 256;
constexpr int kMaxWatchDepth = 16;

DebuggerGDB* g_debugger = nullptr;

const char kSourceRevision[] = CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION;
const char kAbiIdentity[] = CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_IDENTITY;
const char kProviderIdentity[] = CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_PROVIDER_IDENTITY;

void SetError(char** error, const std::string& message)
{
    if (!error) return;
    *error = static_cast<char*>(std::malloc(message.size() + 1));
    if (!*error) return;
    std::memcpy(*error, message.c_str(), message.size() + 1);
}

void SetJson(char** json, const std::string& value)
{
    if (!json) return;
    *json = static_cast<char*>(std::malloc(value.size() + 1));
    if (!*json) return;
    std::memcpy(*json, value.c_str(), value.size() + 1);
}

std::string Escape(const wxString& value)
{
    const wxScopedCharBuffer utf8 = value.utf8_str();
    const std::string source = utf8 ? std::string(utf8.data()) : std::string();
    std::string result;
    result.reserve(source.size() + 8);
    for (const char ch : source) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += ch; break;
        }
    }
    return result;
}

std::string Boolean(bool value)
{
    return value ? "true" : "false";
}

std::string FrameJson(const cbStackFrame& frame)
{
    long line = 0;
    const bool hasLine = frame.GetLine().ToLong(&line);
    return "{\"number\":" + std::to_string(frame.GetNumber()) +
           ",\"address\":" + std::to_string(frame.GetAddress()) +
           ",\"addressText\":\"" + Escape(frame.GetAddressAsString()) +
           "\",\"function\":\"" + Escape(frame.GetSymbol()) +
           "\",\"file\":\"" + Escape(frame.GetFilename()) +
           "\",\"lineText\":\"" + Escape(frame.GetLine()) +
           "\",\"line\":" + (hasLine ? std::to_string(line) : "null") +
           "\",\"valid\":" + Boolean(frame.IsValid()) + "}";
}

std::string ThreadJson(const cbThread& thread)
{
    return "{\"active\":" + Boolean(thread.IsActive()) +
           ",\"number\":" + std::to_string(thread.GetNumber()) +
           ",\"info\":\"" + Escape(thread.GetInfo()) + "}";
}

std::string BreakpointJson(const cbBreakpoint& breakpoint)
{
    return "{\"location\":\"" + Escape(breakpoint.GetLocation()) +
           "\",\"line\":" + std::to_string(breakpoint.GetLine()) +
           ",\"lineText\":\"" + Escape(breakpoint.GetLineString()) +
           "\",\"type\":\"" + Escape(breakpoint.GetType()) +
           "\",\"info\":\"" + Escape(breakpoint.GetInfo()) +
           "\",\"enabled\":" + Boolean(breakpoint.IsEnabled()) +
           ",\"visible\":" + Boolean(breakpoint.IsVisibleInEditor()) +
           ",\"temporary\":" + Boolean(breakpoint.IsTemporary()) + "}";
}

std::string WatchJson(const cb::shared_ptr<cbWatch>& watch, int depth)
{
    if (!watch) return "null";
    if (depth > kMaxWatchDepth) return "{\"truncated\":true}";

    wxString symbol;
    wxString value;
    wxString type;
    wxString full;
    watch->GetSymbol(symbol);
    watch->GetValue(value);
    watch->GetType(type);
    watch->GetFullWatchString(full);

    std::string result = "{\"symbol\":\"" + Escape(symbol) +
        "\",\"value\":\"" + Escape(value) +
        "\",\"type\":\"" + Escape(type) +
        "\",\"full\":\"" + Escape(full) +
        "\",\"address\":" + std::to_string(watch->GetAddress()) +
        ",\"valueError\":" + Boolean(watch->GetIsValueErrorMessage()) +
        ",\"changed\":" + Boolean(watch->IsChanged()) +
        ",\"expanded\":" + Boolean(watch->IsExpanded()) +
        ",\"children\":[";
    const int childCount = std::min(watch->GetChildCount(), static_cast<int>(kMaxItems));
    for (int index = 0; index < childCount; ++index) {
        if (index) result += ',';
        result += WatchJson(watch->GetChild(index), depth + 1);
    }
    if (watch->GetChildCount() > childCount) {
        if (childCount) result += ',';
        result += "{\"truncated\":true}";
    }
    result += "]}";
    return result;
}

bool Attach(void* opaqueDebuggerPlugin,
            std::uint32_t expectedSdkMajor,
            std::uint32_t expectedSdkMinor,
            std::uint32_t expectedSdkRelease,
            const char* expectedSourceRevision,
            const char* expectedAbiIdentity,
            char** error)
{
    try {
    if (expectedSdkMajor != CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MAJOR ||
        expectedSdkMinor != CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MINOR ||
        expectedSdkRelease != CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_RELEASE) {
        SetError(error, "DebuggerGDB provider SDK tuple does not match the adapter build.");
        return false;
    }
    if (!expectedSourceRevision || std::string(expectedSourceRevision) != kSourceRevision) {
        SetError(error, "DebuggerGDB provider source revision does not match the adapter build.");
        return false;
    }
    if (!expectedAbiIdentity || std::string(expectedAbiIdentity) != kAbiIdentity) {
        SetError(error, "DebuggerGDB provider ABI identity does not match the adapter build.");
        return false;
    }
    if (!opaqueDebuggerPlugin) {
        SetError(error, "The Code::Blocks Debugger plugin pointer is null.");
        return false;
    }

    auto* publicPlugin = static_cast<cbDebuggerPlugin*>(opaqueDebuggerPlugin);
    auto* privatePlugin = dynamic_cast<DebuggerGDB*>(publicPlugin);
    if (!privatePlugin) {
        SetError(error, "The selected Debugger plugin is not the matched DebuggerGDB implementation.");
        return false;
    }
        g_debugger = privatePlugin;
        return true;
    } catch (const std::exception& exception) {
        SetError(error, std::string("DebuggerGDB provider attach failed: ") + exception.what());
        return false;
    } catch (...) {
        SetError(error, "DebuggerGDB provider attach failed with an unknown exception.");
        return false;
    }
}

void Detach()
{
    g_debugger = nullptr;
}

bool Snapshot(const char* dataKind, const char* expression, char** json, char** error)
{
    if (json) *json = nullptr;
    if (error) *error = nullptr;
    try {
    if (!g_debugger) {
        SetError(error, "The DebuggerGDB provider is not attached.");
        return false;
    }
    const std::string kind = dataKind ? dataKind : "";
    if (kind == "frames") {
        const int total = std::max(0, g_debugger->GetStackFrameCount());
        const int count = std::min(total, static_cast<int>(kMaxItems));
        std::string value = "{\"dataKind\":\"frames\",\"activeFrame\":" +
            std::to_string(g_debugger->GetActiveStackFrame()) + ",\"items\":[";
        for (int index = 0; index < count; ++index) {
            if (index) value += ',';
            const auto frame = g_debugger->GetStackFrame(index);
            value += frame ? FrameJson(*frame) : "null";
        }
        if (total > count) {
            if (count) value += ',';
            value += "{\"truncated\":true}";
        }
        value += "]}";
        SetJson(json, value);
        return json && *json;
    }
    if (kind == "threads") {
        const int total = std::max(0, g_debugger->GetThreadsCount());
        const int count = std::min(total, static_cast<int>(kMaxItems));
        std::string value = "{\"dataKind\":\"threads\",\"items\":[";
        for (int index = 0; index < count; ++index) {
            if (index) value += ',';
            const auto thread = g_debugger->GetThread(index);
            value += thread ? ThreadJson(*thread) : "null";
        }
        if (total > count) {
            if (count) value += ',';
            value += "{\"truncated\":true}";
        }
        value += "]}";
        SetJson(json, value);
        return json && *json;
    }
    if (kind == "breakpoints") {
        const int total = std::max(0, g_debugger->GetBreakpointsCount());
        const int count = std::min(total, static_cast<int>(kMaxItems));
        std::string value = "{\"dataKind\":\"breakpoints\",\"items\":[";
        for (int index = 0; index < count; ++index) {
            if (index) value += ',';
            const auto breakpoint = g_debugger->GetBreakpoint(index);
            value += breakpoint ? BreakpointJson(*breakpoint) : "null";
        }
        if (total > count) {
            if (count) value += ',';
            value += "{\"truncated\":true}";
        }
        value += "]}";
        SetJson(json, value);
        return json && *json;
    }
    if (kind == "watches" || kind == "variables") {
        const wxString symbol = wxString::FromUTF8(expression ? expression : "");
        if (symbol.empty()) {
            SetError(error, "An expression is required for the DebuggerGDB watch provider.");
            return false;
        }
        auto watch = g_debugger->AddWatch(symbol, true);
        if (!watch) {
            SetError(error, "DebuggerGDB rejected the requested watch expression.");
            return false;
        }
        g_debugger->UpdateWatch(watch);
        const std::string value = "{\"dataKind\":\"" + kind + "\",\"expression\":\"" +
            Escape(symbol) + "\",\"item\":" + WatchJson(watch, 0) + "}";
        g_debugger->DeleteWatch(watch);
        SetJson(json, value);
        return json && *json;
    }

        SetError(error, "The DebuggerGDB provider does not implement the requested data kind.");
        return false;
    } catch (const std::exception& exception) {
        SetError(error, std::string("DebuggerGDB provider snapshot failed: ") + exception.what());
        return false;
    } catch (...) {
        SetError(error, "DebuggerGDB provider snapshot failed with an unknown exception.");
        return false;
    }
}

void FreeString(char* value)
{
    std::free(value);
}

const codium::CodeBlocksDebuggerProviderApi kApi{
    kApiMajor,
    kApiMinor,
    CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MAJOR,
    CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_MINOR,
    CODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SDK_RELEASE,
    kSourceRevision,
    kAbiIdentity,
    kProviderIdentity,
    &Attach,
    &Detach,
    &Snapshot,
    &FreeString
};

} // namespace

extern "C" CODIUM_BLOCKS_DEBUGGER_PROVIDER_EXPORT
const codium::CodeBlocksDebuggerProviderApi* codium_blocks_debugger_provider_get_api()
{
    return &kApi;
}
