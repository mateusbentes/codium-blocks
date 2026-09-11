// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/syntax_highlighting.hpp"

#include <wx/init.h>

#include <iostream>

namespace {

bool HasToken(const std::vector<codium::SyntaxToken>& tokens, codium::SyntaxTokenKind kind,
             long start, long length)
{
    for (const auto& token : tokens) {
        if (token.kind == kind && token.start == start && token.length == length) return true;
    }
    return false;
}

} // namespace

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "syntax-highlighting-smoke: wxWidgets initialization failed\n";
        return 1;
    }

    const wxString cpp = wxS("#include <vector>\nint main() { // note\n  const int answer = 42;\n  return answer;\n}\n");
    const auto cppTokens = codium::SyntaxHighlighter::Tokenize(cpp, wxS("cpp"));
    if (!HasToken(cppTokens, codium::SyntaxTokenKind::Preprocessor, 0, 17) ||
        !HasToken(cppTokens, codium::SyntaxTokenKind::Type, 18, 3) ||
        !HasToken(cppTokens, codium::SyntaxTokenKind::Function, 22, 4) ||
        !HasToken(cppTokens, codium::SyntaxTokenKind::Comment, 31, 7) ||
        !HasToken(cppTokens, codium::SyntaxTokenKind::Number, 60, 2)) {
        std::cerr << "syntax-highlighting-smoke: C++ tokenization failed\n";
        return 2;
    }

    const wxString json = wxS("{\"name\": \"Codium\", \"enabled\": true}\n");
    const auto jsonTokens = codium::SyntaxHighlighter::Tokenize(json, wxS("json"));
    if (!HasToken(jsonTokens, codium::SyntaxTokenKind::Property, 1, 6) ||
        !HasToken(jsonTokens, codium::SyntaxTokenKind::String, 9, 8) ||
        !HasToken(jsonTokens, codium::SyntaxTokenKind::Keyword, 30, 4)) {
        std::cerr << "syntax-highlighting-smoke: JSON tokenization failed\n";
        return 3;
    }

    const wxString markdown = wxS("# Title\nA [link](https://example.test)\n");
    const auto markdownTokens = codium::SyntaxHighlighter::Tokenize(markdown, wxS("markdown"));
    if (!HasToken(markdownTokens, codium::SyntaxTokenKind::Heading, 0, 7)) {
        std::cerr << "syntax-highlighting-smoke: Markdown tokenization failed\n";
        return 4;
    }

    const wxString html = wxS("<script>const answer = 42;</script><style>body { color: red; }</style>");
    const auto htmlTokens = codium::SyntaxHighlighter::Tokenize(html, wxS("html"));
    if (!HasToken(htmlTokens, codium::SyntaxTokenKind::Tag, 0, 8) ||
        !HasToken(htmlTokens, codium::SyntaxTokenKind::Keyword, 8, 5) ||
        !HasToken(htmlTokens, codium::SyntaxTokenKind::Number, 23, 2) ||
        !HasToken(htmlTokens, codium::SyntaxTokenKind::Tag, 26, 9) ||
        !HasToken(htmlTokens, codium::SyntaxTokenKind::Tag, 35, 7) ||
        !HasToken(htmlTokens, codium::SyntaxTokenKind::Property, 49, 5)) {
        std::cerr << "syntax-highlighting-smoke: HTML embedded tokenization failed\n";
        return 5;
    }

    if (!codium::SyntaxHighlighter::Tokenize(wxS("plain text"), wxS("plaintext")).empty()) {
        std::cerr << "syntax-highlighting-smoke: plaintext unexpectedly tokenized\n";
        return 6;
    }

    std::cout << "syntax-highlighting-smoke: ok — native language tokenization\n";
    return 0;
}
