// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include <wx/string.h>

namespace codium {

struct EditorMatch final {
    long start = -1;
    long length = 0;

    bool Found() const { return start >= 0; }
};

struct EditorLineColumn final {
    int line = 0;
    int column = 0;
};

struct EditorDelimiterPair final {
    long first = -1;
    long second = -1;

    bool Found() const { return first >= 0 && second >= 0; }
};

class EditorActions final {
public:
    static EditorMatch Find(const wxString& text, const wxString& query,
                            long start = 0, bool backwards = false,
                            bool matchCase = true);
    static wxString ReplaceAll(const wxString& text, const wxString& query,
                               const wxString& replacement, bool matchCase = true,
                               int* replacementCount = nullptr);
    static long PositionForLineColumn(const wxString& text, int line, int column);
    static EditorLineColumn LineColumnForPosition(const wxString& text, long position);
    static EditorDelimiterPair MatchingDelimiters(const wxString& text, long caret);
    static wxString IndentationForNewline(const wxString& text, long caret, int indentWidth = 4);
};

} // namespace codium
