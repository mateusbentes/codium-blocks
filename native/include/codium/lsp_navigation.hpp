// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <vector>

namespace codium {

struct LspPosition final {
    int line = 0;
    int character = 0;
};

struct LspRange final {
    LspPosition start;
    LspPosition end;
};

struct LspLocation final {
    wxString uri;
    wxString name;
    wxString detail;
    int kind = 0;
    LspRange range;
};

struct LspTextEdit final {
    wxString uri;
    LspRange range;
    wxString newText;
};

struct LspCompletionItem final {
    wxString label;
    wxString detail;
    wxString documentation;
    wxString insertText;
    LspTextEdit textEdit;
    bool hasTextEdit = false;
};

struct LspCodeAction final {
    wxString title;
    wxString kind;
    std::vector<LspTextEdit> edits;
};

class LspNavigation final {
public:
    static std::vector<LspLocation> LocationsFromResult(const wxString& responseLine,
                                                         bool includeSymbols = false);
    static std::vector<LspCompletionItem> CompletionItemsFromResult(const wxString& responseLine);
    static std::vector<LspCodeAction> CodeActionsFromResult(const wxString& responseLine);
    static std::vector<LspTextEdit> WorkspaceEditsFromResult(const wxString& responseLine);
    static wxString HoverTextFromResult(const wxString& responseLine);
    static wxString UriToPath(const wxString& uri);
    static long OffsetForPosition(const wxString& text, const LspPosition& position);
    static wxString ApplyTextEdits(const wxString& text, const std::vector<LspTextEdit>& edits);
};

} // namespace codium
