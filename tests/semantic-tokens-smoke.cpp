#include "codium/semantic_tokens.hpp"

#include <wx/init.h>

#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "semantic-tokens-smoke: wxWidgets initialization failed\n";
        return 1;
    }

    const wxString initializeLine = wxS(
        "{\"event\":\"languageServerResult\",\"method\":\"initialize\","
        "\"result\":{\"capabilities\":{\"semanticTokensProvider\":{\"legend\":{"
        "\"tokenTypes\":[\"type\",\"function\",\"keyword\",\"number\"],"
        "\"tokenModifiers\":[]},\"full\":true}}}}}");
    const wxArrayString legend = codium::SemanticTokenDecoder::TokenTypesFromInitialize(initializeLine);
    if (legend.size() != 4 || legend[1] != wxS("function")) {
        std::cerr << "semantic-tokens-smoke: initialize legend failed\n";
        return 2;
    }

    const wxString text = wxS("int main()\nreturn 42\n");
    const wxString response = wxS(
        "{\"event\":\"languageServerResult\",\"method\":\"textDocument/semanticTokens/full\","
        "\"result\":{\"data\":[0,0,3,0,0,0,4,4,1,0,1,0,6,2,0,0,7,2,3,0]}}}");
    const auto tokens = codium::SemanticTokenDecoder::DecodeFullResponse(response, legend, text);
    if (tokens.size() != 4 || tokens[0].start != 0 || tokens[0].type != wxS("type") ||
        tokens[1].start != 4 || tokens[1].type != wxS("function") ||
        tokens[2].start != 11 || tokens[2].type != wxS("keyword") ||
        tokens[3].start != 18 || tokens[3].length != 2 || tokens[3].type != wxS("number")) {
        std::cerr << "semantic-tokens-smoke: delta token decoding failed\n";
        return 3;
    }
    if (codium::SemanticTokenDecoder::KindForType(wxS("function")) != codium::SyntaxTokenKind::Function ||
        codium::SemanticTokenDecoder::KindForType(wxS("variable")) != codium::SyntaxTokenKind::Property) {
        std::cerr << "semantic-tokens-smoke: token kind mapping failed\n";
        return 4;
    }

    std::cout << "semantic-tokens-smoke: ok — LSP legend and delta decoding\n";
    return 0;
}
