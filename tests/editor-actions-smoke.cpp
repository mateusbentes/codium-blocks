// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/editor_actions.hpp"

#include <wx/init.h>

#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "editor-actions-smoke: wxWidgets initialization failed\n";
        return 1;
    }

    const wxString text = wxS("Alpha beta\nsecond Alpha line\nthird");
    const auto first = codium::EditorActions::Find(text, wxS("alpha"), 0, false, false);
    const auto second = codium::EditorActions::Find(text, wxS("alpha"), first.start + first.length,
                                                     false, false);
    const auto previous = codium::EditorActions::Find(text, wxS("alpha"), text.length(), true, false);
    if (!first.Found() || first.start != 0 || !second.Found() || second.start != 18 ||
        !previous.Found() || previous.start != second.start) {
        std::cerr << "editor-actions-smoke: search failed\n";
        return 1;
    }

    int replacements = 0;
    const wxString replaced = codium::EditorActions::ReplaceAll(
        text, wxS("alpha"), wxS("value"), false, &replacements);
    if (replacements != 2 || replaced != wxS("value beta\nsecond value line\nthird")) {
        std::cerr << "editor-actions-smoke: replace failed\n";
        return 2;
    }

    const long position = codium::EditorActions::PositionForLineColumn(text, 1, 7);
    const auto location = codium::EditorActions::LineColumnForPosition(text, position);
    if (text.Mid(position, 5) != wxS("Alpha") || location.line != 1 || location.column != 7) {
        std::cerr << "editor-actions-smoke: line/column conversion failed\n";
        return 3;
    }

    if (codium::EditorActions::Find(text, wxEmptyString).Found()) {
        std::cerr << "editor-actions-smoke: empty query matched\n";
        return 4;
    }

    const wxString delimiters = wxS("if (value[0] == 1) { return; }");
    const long brace = delimiters.Find(wxChar('{'));
    const auto pair = codium::EditorActions::MatchingDelimiters(delimiters, brace + 1);
    if (!pair.Found() || pair.first != brace || pair.second != delimiters.Find(wxChar('}'))) {
        std::cerr << "editor-actions-smoke: delimiter matching failed\n";
        return 5;
    }
    if (codium::EditorActions::IndentationForNewline(wxS("if {"), 4) != wxS("    ") ||
        codium::EditorActions::IndentationForNewline(wxS("if {\n    }"), 9) != wxEmptyString) {
        std::cerr << "editor-actions-smoke: automatic indentation failed\n";
        return 6;
    }

    std::cout << "editor-actions-smoke: ok — search, replace, navigation, delimiters, indentation\n";
    return 0;
}
