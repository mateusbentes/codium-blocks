// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/semantic_tokens.hpp"

#include <algorithm>
#include <utility>

namespace codium {
namespace {

wxArrayString JsonStringArrayField(const wxString& line, const wxString& field)
{
    wxArrayString values;
    const wxString marker = wxString::Format(wxS("\"%s\""), field);
    const int start = line.Find(marker);
    if (start == wxNOT_FOUND) return values;
    int index = start + static_cast<int>(marker.length());
    while (index < static_cast<int>(line.length()) &&
           (line[index] == wxChar(' ') || line[index] == wxChar('\t'))) ++index;
    if (index >= static_cast<int>(line.length()) || line[index] != wxChar(':')) return values;
    ++index;
    while (index < static_cast<int>(line.length()) &&
           (line[index] == wxChar(' ') || line[index] == wxChar('\t'))) ++index;
    if (index >= static_cast<int>(line.length()) || line[index] != wxChar('[')) return values;
    ++index;
    while (index < static_cast<int>(line.length())) {
        while (index < static_cast<int>(line.length()) &&
               (line[index] == wxChar(' ') || line[index] == wxChar('\t') || line[index] == wxChar(','))) ++index;
        if (index >= static_cast<int>(line.length()) || line[index] == wxChar(']')) break;
        if (line[index] != wxChar('"')) {
            ++index;
            continue;
        }
        const int valueStart = ++index;
        while (index < static_cast<int>(line.length()) && line[index] != wxChar('"')) {
            if (line[index] == wxChar('\\') && index + 1 < static_cast<int>(line.length())) ++index;
            ++index;
        }
        values.Add(line.Mid(valueStart, index - valueStart));
        if (index < static_cast<int>(line.length())) ++index;
    }
    return values;
}

std::vector<int> JsonIntArrayField(const wxString& line, const wxString& field)
{
    std::vector<int> values;
    const wxString marker = wxString::Format(wxS("\"%s\""), field);
    const int start = line.Find(marker);
    if (start == wxNOT_FOUND) return values;
    int index = start + static_cast<int>(marker.length());
    while (index < static_cast<int>(line.length()) &&
           (line[index] == wxChar(' ') || line[index] == wxChar('\t'))) ++index;
    if (index >= static_cast<int>(line.length()) || line[index] != wxChar(':')) return values;
    ++index;
    while (index < static_cast<int>(line.length()) &&
           (line[index] == wxChar(' ') || line[index] == wxChar('\t'))) ++index;
    if (index >= static_cast<int>(line.length()) || line[index] != wxChar('[')) return values;
    ++index;
    while (index < static_cast<int>(line.length())) {
        while (index < static_cast<int>(line.length()) &&
               (line[index] == wxChar(' ') || line[index] == wxChar('\t') || line[index] == wxChar(','))) ++index;
        if (index >= static_cast<int>(line.length()) || line[index] == wxChar(']')) break;
        int sign = 1;
        if (line[index] == wxChar('-')) {
            sign = -1;
            ++index;
        }
        int value = 0;
        bool found = false;
        while (index < static_cast<int>(line.length()) && line[index] >= wxChar('0') && line[index] <= wxChar('9')) {
            value = value * 10 + static_cast<int>(line[index] - wxChar('0'));
            found = true;
            ++index;
        }
        if (found) values.push_back(sign * value);
        else ++index;
    }
    return values;
}

long PositionToOffset(const wxString& text, int line, int character)
{
    if (line < 0 || character < 0) return -1;
    long offset = 0;
    int currentLine = 0;
    while (currentLine < line) {
        const int nextBreak = text.Mid(offset).Find(wxChar('\n'));
        if (nextBreak == wxNOT_FOUND) return -1;
        offset += nextBreak + 1;
        ++currentLine;
    }
    return std::min<long>(static_cast<long>(text.length()), offset + character);
}

} // namespace

wxArrayString SemanticTokenDecoder::DefaultTokenTypes()
{
    return {
        wxS("namespace"), wxS("type"), wxS("class"), wxS("enum"), wxS("interface"),
        wxS("struct"), wxS("typeParameter"), wxS("parameter"), wxS("variable"),
        wxS("property"), wxS("enumMember"), wxS("event"), wxS("function"), wxS("member"),
        wxS("macro"), wxS("label"), wxS("comment"), wxS("string"), wxS("keyword"),
        wxS("number"), wxS("regexp"), wxS("operator"), wxS("decorator")
    };
}

wxArrayString SemanticTokenDecoder::TokenTypesFromInitialize(const wxString& responseLine)
{
    const wxArrayString values = JsonStringArrayField(responseLine, wxS("tokenTypes"));
    return values.IsEmpty() ? DefaultTokenTypes() : values;
}

std::vector<SemanticToken> SemanticTokenDecoder::DecodeFullResponse(const wxString& responseLine,
                                                                     const wxArrayString& tokenTypes,
                                                                     const wxString& text)
{
    const std::vector<int> data = JsonIntArrayField(responseLine, wxS("data"));
    std::vector<SemanticToken> result;
    if (data.size() < 5) return result;
    int line = 0;
    int character = 0;
    const wxArrayString fallbackTypes = DefaultTokenTypes();
    const wxArrayString& types = tokenTypes.IsEmpty() ? fallbackTypes : tokenTypes;
    for (size_t index = 0; index + 4 < data.size(); index += 5) {
        const int deltaLine = data[index];
        const int deltaStart = data[index + 1];
        if (deltaLine < 0 || deltaStart < 0 || data[index + 2] <= 0) continue;
        line += deltaLine;
        character = deltaLine == 0 ? character + deltaStart : deltaStart;
        const long start = PositionToOffset(text, line, character);
        const long finish = PositionToOffset(text, line, character + data[index + 2]);
        if (start < 0 || finish <= start) continue;
        SemanticToken token;
        token.start = start;
        token.length = finish - start;
        const int typeIndex = data[index + 3];
        token.type = typeIndex >= 0 && typeIndex < static_cast<int>(types.size())
            ? types[static_cast<size_t>(typeIndex)] : wxString::Format(wxS("type-%d"), typeIndex);
        result.push_back(std::move(token));
    }
    return result;
}

SyntaxTokenKind SemanticTokenDecoder::KindForType(const wxString& type)
{
    const wxString normalized = type.Lower();
    if (normalized == wxS("comment")) return SyntaxTokenKind::Comment;
    if (normalized == wxS("string")) return SyntaxTokenKind::String;
    if (normalized == wxS("number")) return SyntaxTokenKind::Number;
    if (normalized == wxS("keyword") || normalized == wxS("operator")) return SyntaxTokenKind::Keyword;
    if (normalized == wxS("type") || normalized == wxS("class") || normalized == wxS("enum") ||
        normalized == wxS("interface") || normalized == wxS("struct") || normalized == wxS("namespace") ||
        normalized == wxS("typeparameter")) return SyntaxTokenKind::Type;
    if (normalized == wxS("function") || normalized == wxS("method") || normalized == wxS("member"))
        return SyntaxTokenKind::Function;
    if (normalized == wxS("macro") || normalized == wxS("decorator")) return SyntaxTokenKind::Preprocessor;
    if (normalized == wxS("property") || normalized == wxS("parameter") || normalized == wxS("variable") ||
        normalized == wxS("enummember") || normalized == wxS("event")) return SyntaxTokenKind::Property;
    return SyntaxTokenKind::Plain;
}

} // namespace codium
