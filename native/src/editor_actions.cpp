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

} // namespace codium
