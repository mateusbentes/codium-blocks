// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/editor_actions.hpp"

#include <algorithm>

namespace codium {
namespace {

wxString SearchText(const wxString& text, bool matchCase)
{
    return matchCase ? text : text.Lower();
}

long ClampPosition(const wxString& text, long position)
{
    return std::max(0L, std::min(position, static_cast<long>(text.length())));
}

} // namespace

EditorMatch EditorActions::Find(const wxString& text, const wxString& query,
                                long start, bool backwards, bool matchCase)
{
    if (query.empty() || text.empty() || query.length() > text.length()) return {};

    const wxString haystack = SearchText(text, matchCase);
    const wxString needle = SearchText(query, matchCase);
    const long textLength = static_cast<long>(text.length());
    const long maxStart = textLength - static_cast<long>(query.length());
    start = std::max(0L, std::min(start, textLength));

    if (backwards) {
        const size_t from = static_cast<size_t>(std::min(start, maxStart));
        const size_t position = haystack.rfind(needle, from);
        if (position == wxString::npos) return {};
        return {static_cast<long>(position), static_cast<long>(query.length())};
    }

    const size_t position = haystack.find(needle, static_cast<size_t>(start));
    if (position == wxString::npos) return {};
    return {static_cast<long>(position), static_cast<long>(query.length())};
}

wxString EditorActions::ReplaceAll(const wxString& text, const wxString& query,
                                   const wxString& replacement, bool matchCase,
                                   int* replacementCount)
{
    if (replacementCount) *replacementCount = 0;
    if (query.empty()) return text;

    const wxString haystack = SearchText(text, matchCase);
    const wxString needle = SearchText(query, matchCase);
    wxString result;
    result.reserve(text.length());
    size_t cursor = 0;
    while (cursor <= text.length()) {
        const size_t position = haystack.find(needle, cursor);
        if (position == wxString::npos) {
            result += text.Mid(cursor);
            break;
        }
        result += text.Mid(cursor, position - cursor);
        result += replacement;
        cursor = position + query.length();
        if (replacementCount) ++(*replacementCount);
    }
    return result;
}

long EditorActions::PositionForLineColumn(const wxString& text, int line, int column)
{
    line = std::max(0, line);
    column = std::max(0, column);
    long position = 0;
    int currentLine = 0;
    while (position < static_cast<long>(text.length()) && currentLine < line) {
        if (text[static_cast<size_t>(position)] == wxChar('\n')) ++currentLine;
        ++position;
    }
    return std::min(static_cast<long>(text.length()), position + column);
}

EditorLineColumn EditorActions::LineColumnForPosition(const wxString& text, long position)
{
    position = ClampPosition(text, position);
    EditorLineColumn result;
    for (long index = 0; index < position; ++index) {
        if (text[static_cast<size_t>(index)] == wxChar('\n')) {
            ++result.line;
            result.column = 0;
        } else {
            ++result.column;
        }
    }
    return result;
}

EditorDelimiterPair EditorActions::MatchingDelimiters(const wxString& text, long caret)
{
    caret = ClampPosition(text, caret);
    long candidate = caret;
    if (candidate >= static_cast<long>(text.length()) ||
        wxString(wxS("([{)]}")).Find(text[static_cast<size_t>(candidate)]) == wxNOT_FOUND) {
        candidate = caret > 0 ? caret - 1 : -1;
    }
    if (candidate < 0 || candidate >= static_cast<long>(text.length())) return {};
    const wxChar opening = text[static_cast<size_t>(candidate)];
    const wxChar closing = opening == wxChar('(') ? wxChar(')') :
                           opening == wxChar('[') ? wxChar(']') :
                           opening == wxChar('{') ? wxChar('}') :
                           opening == wxChar(')') ? wxChar('(') :
                           opening == wxChar(']') ? wxChar('[') :
                           opening == wxChar('}') ? wxChar('{') : wxChar();
    if (closing == wxChar()) return {};
    const bool forward = opening == wxChar('(') || opening == wxChar('[') || opening == wxChar('{');
    int depth = 0;
    if (forward) {
        for (long index = candidate; index < static_cast<long>(text.length()); ++index) {
            if (text[static_cast<size_t>(index)] == opening) ++depth;
            else if (text[static_cast<size_t>(index)] == closing && --depth == 0)
                return {candidate, index};
        }
    } else {
        for (long index = candidate; index >= 0; --index) {
            if (text[static_cast<size_t>(index)] == opening) ++depth;
            else if (text[static_cast<size_t>(index)] == closing && --depth == 0)
                return {index, candidate};
        }
    }
    return {};
}

wxString EditorActions::IndentationForNewline(const wxString& text, long caret, int indentWidth)
{
    caret = ClampPosition(text, caret);
    indentWidth = std::max(1, indentWidth);
    const int lineBreak = text.Left(caret).Find(wxChar('\n'), true);
    const long lineStart = lineBreak == wxNOT_FOUND ? 0 : lineBreak + 1;
    wxString indentation;
    long index = lineStart;
    while (index < caret && (text[static_cast<size_t>(index)] == wxChar(' ') ||
                             text[static_cast<size_t>(index)] == wxChar('\t'))) {
        indentation += text[static_cast<size_t>(index++)];
    }
    long previous = caret - 1;
    while (previous >= lineStart && (text[static_cast<size_t>(previous)] == wxChar(' ') ||
                                     text[static_cast<size_t>(previous)] == wxChar('\t'))) --previous;
    const wxChar next = caret < static_cast<long>(text.length())
        ? static_cast<wxChar>(text[static_cast<size_t>(caret)]) : wxChar();
    const wxChar previousCode = previous >= lineStart
        ? static_cast<wxChar>(text[static_cast<size_t>(previous)]) : wxChar();
    if (previousCode == wxChar('{') || previousCode == wxChar('[') || previousCode == wxChar('(')) {
        for (int count = 0; count < indentWidth; ++count) indentation += wxChar(' ');
    } else if ((next == wxChar('}') || next == wxChar(']') || next == wxChar(')')) &&
               indentation.length() >= static_cast<size_t>(indentWidth)) {
        indentation.Truncate(indentation.length() - static_cast<size_t>(indentWidth));
    }
    return indentation;
}

} // namespace codium
