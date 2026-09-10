// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <map>
#include <vector>

namespace codium {

class SourceMapper final {
public:
    void Add(const wxString& remoteRoot, const wxString& localRoot);
    void Clear();
    wxString Map(const wxString& remotePath) const;
    wxString ToJson() const;
    size_t Size() const { return mappings_.size(); }

private:
    std::map<wxString, wxString> mappings_;
};

class WatchStore final {
public:
    static wxArrayString Load(const wxString& workspaceRoot);
    static bool Save(const wxString& workspaceRoot, const wxArrayString& expressions, wxString* error = nullptr);
};

struct CodeBlocksDebugFrame final {
    int number = 0;
    wxString function;
    wxString file;
    wxString lineText;
    int line = 0;
    bool hasLine = false;
    bool valid = false;
};

struct CodeBlocksDebugThread final {
    bool active = false;
    int number = 0;
    wxString info;
};

struct CodeBlocksDebugBreakpoint final {
    wxString location;
    int line = 0;
    wxString lineText;
    wxString type;
    wxString info;
    bool enabled = false;
    bool visible = false;
    bool temporary = false;
};

struct CodeBlocksDebugValue final {
    wxString symbol;
    wxString value;
    wxString type;
    wxString full;
    bool valueError = false;
    bool changed = false;
    bool expanded = false;
    bool truncated = false;
    std::vector<CodeBlocksDebugValue> children;
};

struct CodeBlocksDebugSnapshot final {
    wxString dataKind;
    wxString expression;
    int activeFrame = 0;
    std::vector<CodeBlocksDebugFrame> frames;
    std::vector<CodeBlocksDebugThread> threads;
    std::vector<CodeBlocksDebugBreakpoint> breakpoints;
    bool hasValue = false;
    CodeBlocksDebugValue value;

    static bool Parse(const wxString& json, CodeBlocksDebugSnapshot* snapshot,
                      wxString* error = nullptr);
};

} // namespace codium
