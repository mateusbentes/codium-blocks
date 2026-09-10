#pragma once

#include "codium/syntax_highlighting.hpp"

#include <wx/arrstr.h>
#include <wx/string.h>

#include <vector>

namespace codium {

struct SemanticToken final {
    long start = 0;
    long length = 0;
    wxString type;
    wxArrayString modifiers;
};

class SemanticTokenDecoder final {
public:
    static wxArrayString DefaultTokenTypes();
    static wxArrayString TokenTypesFromInitialize(const wxString& responseLine);
    static std::vector<SemanticToken> DecodeFullResponse(const wxString& responseLine,
                                                         const wxArrayString& tokenTypes,
                                                         const wxString& text);
    static SyntaxTokenKind KindForType(const wxString& type);
};

} // namespace codium
