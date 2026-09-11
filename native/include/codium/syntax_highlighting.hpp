// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include <wx/string.h>

#include <vector>

namespace codium {

enum class SyntaxTokenKind {
    Plain,
    Comment,
    String,
    Number,
    Keyword,
    Type,
    Function,
    Property,
    Preprocessor,
    Tag,
    Heading,
};

struct SyntaxToken final {
    long start = 0;
    long length = 0;
    SyntaxTokenKind kind = SyntaxTokenKind::Plain;
};

class SyntaxHighlighter final {
public:
    static std::vector<SyntaxToken> Tokenize(const wxString& text, const wxString& languageId);
};

} // namespace codium
