// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/extension_security.hpp"
#include "codium/localization.hpp"
#include "codium/problem_model.hpp"
#include "codium/terminal_screen.hpp"

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <random>
#include <vector>

namespace {

bool SameCell(const codium::TerminalCell& left, const codium::TerminalCell& right)
{
    return left.character == right.character && left.text == right.text &&
           left.foreground == right.foreground && left.background == right.background &&
           left.bold == right.bold && left.underline == right.underline &&
           left.inverse == right.inverse && left.hyperlink == right.hyperlink &&
           left.width == right.width && left.continuation == right.continuation;
}

bool SameScreen(const codium::TerminalScreen& left, const codium::TerminalScreen& right)
{
    if (left.Columns() != right.Columns() || left.Rows() != right.Rows() ||
        left.CursorColumn() != right.CursorColumn() || left.CursorRow() != right.CursorRow() ||
        left.CursorVisible() != right.CursorVisible() || left.AlternateScreen() != right.AlternateScreen() ||
        left.MouseReporting() != right.MouseReporting() || left.SgrMouse() != right.SgrMouse() ||
        left.BracketedPaste() != right.BracketedPaste() ||
        left.SynchronizedUpdates() != right.SynchronizedUpdates() ||
        left.GraphicsDiscarded() != right.GraphicsDiscarded() ||
        left.ScrollbackSize() != right.ScrollbackSize() || left.ScrollOffset() != right.ScrollOffset()) {
        return false;
    }
    for (int row = 0; row < left.Rows(); ++row) {
        for (int column = 0; column < left.Columns(); ++column) {
            if (!SameCell(left.CellAt(column, row), right.CellAt(column, row))) return false;
        }
    }
    return true;
}

bool CheckTerminalFragmentation()
{
    constexpr unsigned int seed = 0xC0D1U;
    std::mt19937 generator(seed);
    const std::vector<wxString> tokens = {
        wxS("alpha"), wxS(" beta"), wxS("\n"), wxS("\r"), wxS("\t"),
        wxS("\x1b[31m"), wxS("\x1b[0m"), wxS("\x1b[2J"), wxS("\x1b[2;4H"),
        wxS("\x1b[?25l"), wxS("\x1b[?25h"), wxS("\x1b[?1006h"), wxS("\x1b[?1006l"),
        wxS("\x1b[?2004h"), wxS("\x1b[?2004l"), wxS("\x1b[?2026h"), wxS("\x1b[?2026l"),
        wxS("\x1b]8;;https://example.test\x07link\x1b]8;;\x07"),
        wxS("\x1bPignored graphics\x1b\\"), wxS("\x1b_unknown"),
    };

    for (int iteration = 0; iteration < 400; ++iteration) {
        wxString input;
        const int tokenCount = 4 + static_cast<int>(generator() % 28U);
        for (int token = 0; token < tokenCount; ++token) {
            if ((generator() % 5U) == 0U) {
                input += wxChar('a' + static_cast<char>(generator() % 26U));
            } else {
                input += tokens[generator() % tokens.size()];
            }
        }

        codium::TerminalScreen whole(32, 10);
        codium::TerminalScreen fragmented(32, 10);
        whole.Feed(input);
        size_t offset = 0;
        while (offset < input.length()) {
            const size_t remaining = input.length() - offset;
            const size_t chunkSize = std::min(remaining, 1U + static_cast<size_t>(generator() % 7U));
            fragmented.Feed(input.Mid(offset, chunkSize));
            offset += chunkSize;
        }
        if (!SameScreen(whole, fragmented)) {
            std::cerr << "invariants-smoke: terminal fragmentation property failed at iteration "
                      << iteration << " (seed=" << seed << ")\n";
            return false;
        }
    }
    return true;
}

bool CheckProblemParser()
{
    constexpr unsigned int seed = 0xB10CU;
    std::mt19937 generator(seed);
    const wxString workspace = wxS("C:/codium-blocks/invariants");
    for (int iteration = 0; iteration < 500; ++iteration) {
        const int line = 1 + static_cast<int>(generator() % 10000U);
        const int column = 1 + static_cast<int>(generator() % 240U);
        const int fileIndex = static_cast<int>(generator() % 31U);
        const wxString path = wxString::Format(wxS("src/file%d.cpp"), fileIndex);
        const wxString raw = wxString::Format(
            wxS("%s:%d:%d: warning: generated diagnostic %d"), path, line, column, iteration);
        codium::Problem problem;
        if (!codium::ProblemParser::ParseCompilerLine(raw, wxS("invariants"), workspace, &problem)) {
            std::cerr << "invariants-smoke: valid compiler diagnostic was rejected at iteration "
                      << iteration << " (seed=" << seed << ")\n";
            return false;
        }
        if (problem.severity != codium::ProblemSeverity::Warning || problem.line != line - 1 ||
            problem.column != column - 1 || !problem.path.EndsWith(path) ||
            problem.message != wxString::Format(wxS("generated diagnostic %d"), iteration)) {
            std::cerr << "invariants-smoke: diagnostic normalization property failed at iteration "
                      << iteration << " (seed=" << seed << ")\n";
            return false;
        }

        codium::BuildDiagnosticParser buildParser;
        codium::Problem ansiProblem;
        const wxString ansi = wxString::Format(wxS("\x1b[31m[stderr] %s\x1b[0m"), raw);
        if (!buildParser.ParseLine(ansi, wxS("build"), workspace, &ansiProblem, wxS("session")) ||
            ansiProblem.buildSessionId != wxS("session") || ansiProblem.line != line - 1) {
            std::cerr << "invariants-smoke: ANSI diagnostic property failed at iteration "
                      << iteration << " (seed=" << seed << ")\n";
            return false;
        }
    }
    return true;
}

bool CheckManifestValidation()
{
    constexpr unsigned int seed = 0xE571U;
    std::mt19937 generator(seed);
    for (int iteration = 0; iteration < 400; ++iteration) {
        const int suffix = static_cast<int>(generator() % 100000U);
        const wxString name = wxString::Format(wxS("extension-%d"), suffix);
        const wxString publisher = wxString::Format(wxS("publisher-%d"), suffix);
        const wxString version = wxString::Format(wxS("1.%d.0"), suffix % 1000);
        const wxString valid = wxString::Format(
            wxS("{\"name\":\"%s\",\"publisher\":\"%s\",\"version\":\"%s\"}"),
            name, publisher, version);
        codium::ExtensionManifest manifest;
        wxString error;
        if (!codium::ExtensionSecurity::ValidateManifest(valid, &manifest, &error) ||
            manifest.name != name || manifest.publisher != publisher || manifest.version != version) {
            std::cerr << "invariants-smoke: generated safe manifest was rejected at iteration "
                      << iteration << " (seed=" << seed << ")\n";
            return false;
        }

        const wxString unsafe = wxString::Format(
            wxS("{\"name\":\"%s..escape\",\"publisher\":\"%s\",\"version\":\"%s\"}"),
            name, publisher, version);
        if (codium::ExtensionSecurity::ValidateManifest(unsafe, &manifest, &error)) {
            std::cerr << "invariants-smoke: unsafe manifest was accepted at iteration "
                      << iteration << " (seed=" << seed << ")\n";
            return false;
        }
    }
    return true;
}

bool CheckLocalization(const wxString& sourceRoot)
{
    wxString previousLanguage;
    const bool hadPreviousLanguage = wxGetEnv(wxS("CODIUM_BLOCKS_LANGUAGE"), &previousLanguage);
    wxUnsetEnv(wxS("CODIUM_BLOCKS_LANGUAGE"));

    const wxString dataRoot = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH +
        wxString::Format(wxS("codium-blocks-invariants-%lu"), static_cast<unsigned long>(wxGetProcessId()));
    wxFileName::Mkdir(dataRoot, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    codium::Localization localization;
    wxString error;
    if (!localization.Load(sourceRoot, dataRoot, &error) ||
        !localization.SetLanguage(codium::UiLanguage::English, &error) ||
        localization.Text(wxS("menu.file")) != wxS("&File") ||
        localization.Text(wxS("missing.invariant"), wxS("fallback")) != wxS("fallback")) {
        std::cerr << "invariants-smoke: English localization invariant failed: "
                  << error.ToStdString() << "\n";
        return false;
    }
    if (!localization.SetLanguage(codium::UiLanguage::PortugueseBrazil, &error) ||
        localization.Text(wxS("menu.file")) == wxS("&File") ||
        localization.Text(wxS("about.body")) != wxS("Codium::Blocks")) {
        std::cerr << "invariants-smoke: Portuguese localization invariant failed: "
                  << error.ToStdString() << "\n";
        return false;
    }

    std::filesystem::remove_all(dataRoot.ToStdString());
    if (hadPreviousLanguage) wxSetEnv(wxS("CODIUM_BLOCKS_LANGUAGE"), previousLanguage);
    else wxUnsetEnv(wxS("CODIUM_BLOCKS_LANGUAGE"));
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return 1;
    if (argc < 2) {
        std::cerr << "invariants-smoke: source root argument is required\n";
        return 2;
    }

    const wxString sourceRoot = wxString::FromUTF8(argv[1]);
    if (!CheckTerminalFragmentation() || !CheckProblemParser() ||
        !CheckManifestValidation() || !CheckLocalization(sourceRoot)) {
        return 3;
    }

    std::cout << "invariants-smoke: ok — deterministic terminal, diagnostics, manifest, and localization properties\n";
    return 0;
}
