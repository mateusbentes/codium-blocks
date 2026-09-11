// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/lsp_navigation.hpp"

#include <wx/init.h>

#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "lsp-navigation-smoke: wxWidgets initialization failed\n";
        return 1;
    }

    const wxString locationResponse = wxS(
        "{\"event\":\"languageServerResult\",\"method\":\"textDocument/definition\","
        "\"result\":{\"uri\":\"file:///workspace/include/api.hpp\",\"range\":{"
        "\"start\":{\"line\":4,\"character\":2},\"end\":{\"line\":4,\"character\":8}}}}");
    const auto locations = codium::LspNavigation::LocationsFromResult(locationResponse);
    if (locations.size() != 1 || locations[0].uri != wxS("file:///workspace/include/api.hpp") ||
        locations[0].range.start.line != 4 || locations[0].range.start.character != 2) {
        std::cerr << "lsp-navigation-smoke: location parsing failed\n";
        return 2;
    }

    const wxString symbolsResponse = wxS(
        "{\"result\":[{\"name\":\"main\",\"kind\":12,\"range\":{"
        "\"start\":{\"line\":0,\"character\":0},\"end\":{\"line\":2,\"character\":1}},"
        "\"children\":[{\"name\":\"value\",\"kind\":13,\"selectionRange\":{"
        "\"start\":{\"line\":1,\"character\":4},\"end\":{\"line\":1,\"character\":9}}}]}]}");
    const auto symbols = codium::LspNavigation::LocationsFromResult(symbolsResponse, true);
    if (symbols.size() != 2 || symbols[1].name != wxS("value") || symbols[1].range.start.line != 1) {
        std::cerr << "lsp-navigation-smoke: document symbol parsing failed\n";
        return 3;
    }

    const wxString completionResponse = wxS(
        "{\"result\":{\"items\":[{\"label\":\"printf\",\"detail\":\"function\","
        "\"documentation\":{\"kind\":\"markdown\",\"value\":\"Print **text**\"},"
        "\"textEdit\":{\"range\":{\"start\":{\"line\":0,\"character\":0},"
        "\"end\":{\"line\":0,\"character\":3}},\"newText\":\"printf()\"}}]}}");
    const auto completions = codium::LspNavigation::CompletionItemsFromResult(completionResponse);
    if (completions.size() != 1 || completions[0].label != wxS("printf") ||
        completions[0].documentation != wxS("Print **text**") || !completions[0].hasTextEdit) {
        std::cerr << "lsp-navigation-smoke: completion parsing failed\n";
        return 4;
    }

    const wxString hoverResponse = wxS(
        "{\"result\":{\"contents\":[{\"language\":\"cpp\",\"value\":\"int main()\"},"
        "\"Documentation paragraph\"]}}");
    const wxString hover = codium::LspNavigation::HoverTextFromResult(hoverResponse);
    if (hover.Find(wxS("```cpp")) == wxNOT_FOUND || hover.Find(wxS("Documentation paragraph")) == wxNOT_FOUND) {
        std::cerr << "lsp-navigation-smoke: hover rendering failed\n";
        return 5;
    }

    const wxString actionResponse = wxS(
        "{\"result\":[{\"title\":\"Add include\",\"kind\":\"quickfix\",\"edit\":{"
        "\"changes\":{\"file:///workspace/main.cpp\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},"
        "\"end\":{\"line\":0,\"character\":0}},\"newText\":\"#include <x>\\n\"}]}}}]}");
    const auto actions = codium::LspNavigation::CodeActionsFromResult(actionResponse);
    if (actions.size() != 1 || actions[0].title != wxS("Add include") || actions[0].edits.size() != 1) {
        std::cerr << "lsp-navigation-smoke: code action parsing failed\n";
        return 6;
    }

    const wxString renameResponse = wxS(
        "{\"result\":{\"changes\":{\"file:///workspace/main.cpp\":[{\"range\":{"
        "\"start\":{\"line\":0,\"character\":4},\"end\":{\"line\":0,\"character\":8}},"
        "\"newText\":\"run\"}]}}}");
    const auto edits = codium::LspNavigation::WorkspaceEditsFromResult(renameResponse);
    if (edits.size() != 1 || codium::LspNavigation::ApplyTextEdits(wxS("int main()"), edits) != wxS("int run()")) {
        std::cerr << "lsp-navigation-smoke: workspace edit application failed\n";
        return 7;
    }
    const wxString documentChangesResponse = wxS(
        "{\"result\":{\"documentChanges\":[{\"textDocument\":{\"uri\":\"file:///workspace/main.cpp\"},"
        "\"edits\":[{\"range\":{\"start\":{\"line\":0,\"character\":0},"
        "\"end\":{\"line\":0,\"character\":3}},\"newText\":\"long\"}]}]}}");
    const auto documentChanges = codium::LspNavigation::WorkspaceEditsFromResult(documentChangesResponse);
    if (documentChanges.size() != 1 || documentChanges[0].uri != wxS("file:///workspace/main.cpp")) {
        std::cerr << "lsp-navigation-smoke: documentChanges parsing failed\n";
        return 8;
    }
    if (codium::LspNavigation::UriToPath(wxS("file:///tmp/a%20b.cpp")) != wxS("/tmp/a b.cpp")) {
        std::cerr << "lsp-navigation-smoke: URI decoding failed\n";
        return 9;
    }

    std::cout << "lsp-navigation-smoke: ok — locations, symbols, completion, hover, actions, edits\n";
    return 0;
}
