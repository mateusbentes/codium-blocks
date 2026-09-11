// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/syntax_highlighting.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace codium {
namespace {

using Kind = SyntaxTokenKind;

struct RuleSet final {
    std::set<wxString> keywords;
    std::set<wxString> types;
    wxString lineComment;
    bool hashComment = false;
    bool slashComment = false;
    bool slashBlockComment = false;
    bool markup = false;
    bool markdown = false;
    bool yaml = false;
    bool cmake = false;
};

bool IsIdentifierStart(wxChar character)
{
    return character == wxChar('_') || std::isalpha(static_cast<unsigned char>(character));
}

bool IsIdentifierPart(wxChar character)
{
    return character == wxChar('_') || std::isalnum(static_cast<unsigned char>(character));
}

bool IsNumberStart(const wxString& text, size_t index)
{
    if (index >= text.length()) return false;
    if (std::isdigit(static_cast<unsigned char>(text[index]))) return true;
    return text[index] == wxChar('.') && index + 1 < text.length() &&
           std::isdigit(static_cast<unsigned char>(text[index + 1]));
}

void AddToken(std::vector<SyntaxToken>* tokens, size_t start, size_t end, Kind kind)
{
    if (!tokens || end <= start) return;
    tokens->push_back(SyntaxToken{static_cast<long>(start), static_cast<long>(end - start), kind});
}

RuleSet RulesFor(const wxString& language)
{
    RuleSet rules;
    rules.keywords = {
        wxS("alignas"), wxS("alignof"), wxS("and"), wxS("as"), wxS("asm"), wxS("async"),
        wxS("await"), wxS("break"), wxS("case"), wxS("catch"), wxS("class"), wxS("co_await"),
        wxS("co_return"), wxS("co_yield"), wxS("const"), wxS("consteval"), wxS("constexpr"),
        wxS("constinit"), wxS("continue"), wxS("crate"), wxS("default"), wxS("delete"),
        wxS("do"), wxS("else"), wxS("enum"), wxS("except"), wxS("export"), wxS("extends"),
        wxS("extern"), wxS("finally"), wxS("for"), wxS("fn"), wxS("from"), wxS("function"),
        wxS("global"), wxS("goto"), wxS("if"), wxS("impl"), wxS("import"), wxS("in"),
        wxS("inline"), wxS("interface"), wxS("let"), wxS("match"), wxS("module"), wxS("mutable"),
        wxS("namespace"), wxS("new"), wxS("noexcept"), wxS("not"), wxS("null"), wxS("nullptr"),
        wxS("of"), wxS("operator"), wxS("or"), wxS("override"), wxS("package"), wxS("pass"),
        wxS("private"), wxS("protected"), wxS("pub"), wxS("public"), wxS("raise"), wxS("return"),
        wxS("self"), wxS("static"), wxS("struct"), wxS("super"), wxS("switch"), wxS("template"),
        wxS("this"), wxS("throw"), wxS("trait"), wxS("try"), wxS("type"), wxS("typeof"),
        wxS("union"), wxS("unsafe"), wxS("use"), wxS("using"), wxS("virtual"), wxS("void"),
        wxS("volatile"), wxS("where"), wxS("while"), wxS("with"), wxS("yield"),
    };
    rules.types = {
        wxS("bool"), wxS("char"), wxS("double"), wxS("f32"), wxS("f64"), wxS("float"),
        wxS("i8"), wxS("i16"), wxS("i32"), wxS("i64"), wxS("int"), wxS("long"), wxS("map"),
        wxS("size_t"), wxS("str"), wxS("string"), wxS("u8"), wxS("u16"), wxS("u32"),
        wxS("u64"), wxS("uint"), wxS("unsigned"), wxS("usize"), wxS("vec"), wxS("vector"),
    };

    if (language == wxS("python") || language == wxS("yaml") || language == wxS("cmake")) {
        rules.hashComment = true;
    }
    if (language == wxS("python")) {
        rules.keywords.insert(wxS("def"));
        rules.keywords.insert(wxS("lambda"));
        rules.keywords.insert(wxS("elif"));
        rules.keywords.insert(wxS("is"));
        rules.keywords.insert(wxS("None"));
        rules.keywords.insert(wxS("True"));
        rules.keywords.insert(wxS("False"));
    }
    if (language == wxS("json")) {
        rules.keywords = {wxS("true"), wxS("false"), wxS("null")};
    }
    if (language == wxS("javascript") || language == wxS("javascriptreact") ||
        language == wxS("typescript")) {
        rules.slashComment = true;
        rules.slashBlockComment = true;
        rules.keywords.insert(wxS("const"));
        rules.keywords.insert(wxS("var"));
        rules.keywords.insert(wxS("NaN"));
        rules.keywords.insert(wxS("true"));
        rules.keywords.insert(wxS("false"));
    }
    if (language == wxS("c") || language == wxS("cpp") || language == wxS("rust") ||
        language == wxS("java")) {
        rules.slashComment = true;
        rules.slashBlockComment = true;
    }
    if (language == wxS("html")) {
        rules.markup = true;
        rules.slashComment = false;
    }
    if (language == wxS("css")) {
        rules.slashBlockComment = true;
    }
    if (language == wxS("markdown")) rules.markdown = true;
    if (language == wxS("yaml")) rules.yaml = true;
    if (language == wxS("cmake")) rules.cmake = true;
    return rules;
}

} // namespace

std::vector<SyntaxToken> SyntaxHighlighter::Tokenize(const wxString& text,
                                                       const wxString& languageId)
{
    std::vector<SyntaxToken> tokens;
    if (text.empty() || languageId == wxS("plaintext")) return tokens;
    const RuleSet rules = RulesFor(languageId);
    size_t index = 0;
    bool lineStart = true;

    while (index < text.length()) {
        if (text[index] == wxChar('\n')) {
            ++index;
            lineStart = true;
            continue;
        }
        if (rules.markdown && lineStart && text[index] == wxChar('#')) {
            const size_t start = index;
            while (index < text.length() && text[index] != wxChar('\n')) ++index;
            AddToken(&tokens, start, index, Kind::Heading);
            lineStart = false;
            continue;
        }
        if (rules.yaml && lineStart && (text[index] == wxChar('-') || text[index] == wxChar('?'))) {
            AddToken(&tokens, index, index + 1, Kind::Keyword);
            ++index;
            lineStart = false;
            continue;
        }
        if (rules.cmake && lineStart && text[index] == wxChar('[')) {
            const size_t start = index;
            while (index < text.length() && text[index] != wxChar(']')) ++index;
            if (index < text.length()) ++index;
            AddToken(&tokens, start, index, Kind::Preprocessor);
            lineStart = false;
            continue;
        }
        if (lineStart && text[index] == wxChar('#') &&
            (languageId == wxS("c") || languageId == wxS("cpp") || languageId == wxS("rust"))) {
            const size_t start = index;
            while (index < text.length() && text[index] != wxChar('\n')) ++index;
            AddToken(&tokens, start, index, Kind::Preprocessor);
            lineStart = false;
            continue;
        }
        if (rules.hashComment && text[index] == wxChar('#')) {
            const size_t start = index;
            while (index < text.length() && text[index] != wxChar('\n')) ++index;
            AddToken(&tokens, start, index, Kind::Comment);
            lineStart = false;
            continue;
        }
        if (rules.slashComment && index + 1 < text.length() && text[index] == wxChar('/') &&
            text[index + 1] == wxChar('/')) {
            const size_t start = index;
            while (index < text.length() && text[index] != wxChar('\n')) ++index;
            AddToken(&tokens, start, index, Kind::Comment);
            lineStart = false;
            continue;
        }
        if (rules.slashBlockComment && index + 1 < text.length() && text[index] == wxChar('/') &&
            text[index + 1] == wxChar('*')) {
            const size_t start = index;
            index += 2;
            while (index + 1 < text.length() && !(text[index] == wxChar('*') && text[index + 1] == wxChar('/'))) ++index;
            index = std::min(text.length(), index + 2);
            AddToken(&tokens, start, index, Kind::Comment);
            lineStart = false;
            continue;
        }
        if (rules.markup && text[index] == wxChar('<')) {
            const size_t start = index;
            while (index < text.length() && text[index] != wxChar('>')) ++index;
            if (index < text.length()) ++index;
            AddToken(&tokens, start, index, Kind::Tag);
            const wxString openingTag = text.Mid(start, index - start).Lower();
            const wxString embeddedLanguage = openingTag.StartsWith(wxS("<script"))
                ? wxString(wxS("javascript"))
                : openingTag.StartsWith(wxS("<style")) ? wxString(wxS("css")) : wxString();
            if (!embeddedLanguage.empty()) {
                const wxString closingTag = embeddedLanguage == wxS("javascript")
                    ? wxString(wxS("</script")) : wxString(wxS("</style"));
                const int relativeEnd = text.Mid(index).Lower().Find(closingTag);
                if (relativeEnd != wxNOT_FOUND) {
                    const size_t bodyEnd = index + static_cast<size_t>(relativeEnd);
                    const auto embeddedTokens = Tokenize(text.Mid(index, bodyEnd - index), embeddedLanguage);
                    for (const auto& token : embeddedTokens) {
                        AddToken(&tokens, index + static_cast<size_t>(token.start),
                                 index + static_cast<size_t>(token.start + token.length), token.kind);
                    }
                    index = bodyEnd;
                }
            }
            lineStart = false;
            continue;
        }
        if (text[index] == wxChar('"') || text[index] == wxChar('\'')) {
            const wxChar quote = text[index];
            const size_t start = index++;
            bool escaped = false;
            while (index < text.length()) {
                const wxChar character = text[index++];
                if (character == wxChar('\n')) break;
                if (character == quote && !escaped) break;
                escaped = character == wxChar('\\') && !escaped;
                if (character != wxChar('\\')) escaped = false;
            }
            Kind kind = Kind::String;
            size_t next = index;
            while (next < text.length() && (text[next] == wxChar(' ') || text[next] == wxChar('\t'))) ++next;
            if (languageId == wxS("json") && next < text.length() && text[next] == wxChar(':'))
                kind = Kind::Property;
            AddToken(&tokens, start, index, kind);
            lineStart = false;
            continue;
        }
        if (IsNumberStart(text, index)) {
            const size_t start = index++;
            while (index < text.length() &&
                   (std::isalnum(static_cast<unsigned char>(text[index])) || text[index] == wxChar('.') ||
                    text[index] == wxChar('_'))) ++index;
            AddToken(&tokens, start, index, Kind::Number);
            lineStart = false;
            continue;
        }
        if (rules.markup && text[index] == wxChar('@')) {
            const size_t start = index++;
            while (index < text.length() && IsIdentifierPart(text[index])) ++index;
            AddToken(&tokens, start, index, Kind::Property);
            lineStart = false;
            continue;
        }
        if (IsIdentifierStart(text[index])) {
            const size_t start = index++;
            while (index < text.length() && IsIdentifierPart(text[index])) ++index;
            const wxString word = text.Mid(start, index - start);
            Kind kind = Kind::Plain;
            if (rules.keywords.find(word) != rules.keywords.end()) kind = Kind::Keyword;
            else if (rules.types.find(word) != rules.types.end()) kind = Kind::Type;
            else {
                size_t next = index;
                while (next < text.length() && (text[next] == wxChar(' ') || text[next] == wxChar('\t'))) ++next;
                if (next < text.length() && text[next] == wxChar('(')) kind = Kind::Function;
                else if ((languageId == wxS("json") || languageId == wxS("css")) &&
                         next < text.length() && text[next] == wxChar(':'))
                    kind = Kind::Property;
            }
            if (kind != Kind::Plain) AddToken(&tokens, start, index, kind);
            lineStart = false;
            continue;
        }
        if (text[index] != wxChar(' ') && text[index] != wxChar('\t') && text[index] != wxChar('\r'))
            lineStart = false;
        ++index;
    }
    return tokens;
}

} // namespace codium
