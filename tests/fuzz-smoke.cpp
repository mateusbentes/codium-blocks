// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/dap_client.hpp"
#include "codium/extension_security.hpp"
#include "codium/terminal_screen.hpp"
#include "codium/vsix_manager.hpp"

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <random>
#include <string>
#include <utility>

namespace {

constexpr uint32_t kSeed = 0xC0D1U;

int Iterations()
{
    const char* value = std::getenv("CODIUM_BLOCKS_FUZZ_ITERATIONS");
    if (!value || !*value) return 128;
    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (!end || *end != '\0') return 128;
    return static_cast<int>(std::clamp(parsed, 16L, 2000L));
}

uint32_t Next(std::mt19937& generator)
{
    return generator();
}

std::string Frame(const std::string& body)
{
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

bool RemoveTreeNoThrow(const wxString& path)
{
    std::error_code error;
    std::filesystem::remove_all(path.ToStdString(), error);
    return !error;
}

bool RemoveFileNoThrow(const wxString& path)
{
    std::error_code error;
    std::filesystem::remove(path.ToStdString(), error);
    return !error;
}

bool RunDapFuzz(int iterations, std::mt19937& generator)
{
    for (int iteration = 0; iteration < iterations; ++iteration) {
        std::string payload;
        switch (iteration % 7) {
        case 0:
            payload = Frame("{\"type\":\"event\",\"event\":\"output\"}");
            break;
        case 1:
            payload = "content-length: 2\r\nX-Fuzz: yes\r\n\r\n{}";
            break;
        case 2:
            payload = "Content-Length: invalid\r\n\r\n{}";
            break;
        case 3:
            payload = "Content-Length: 999999999999999999999999\r\n\r\n";
            break;
        case 4:
            payload.assign(65540, 'A');
            payload += "\r\n\r\n";
            break;
        default: {
            const size_t size = static_cast<size_t>(Next(generator) % 768U);
            payload.resize(size);
            for (char& byte : payload) byte = static_cast<char>(Next(generator) & 0xffU);
            break;
        }
        }

        codium::DapClient client(nullptr, wxID_HIGHEST + 1100 + iteration);
        wxArrayString messages;
        const auto feed = [&](const std::string& chunk) {
            const wxArrayString parsed = client.ParseBytesForTesting(chunk);
            for (const auto& message : parsed) messages.Add(message);
        };
        if (iteration % 2 == 0 || payload.size() < 2) {
            feed(payload);
        } else {
            const size_t split = payload.size() / 2;
            feed(payload.substr(0, split));
            feed(payload.substr(split));
        }
        if (iteration % 7 <= 1 && (messages.size() != 1 || !client.LastError().empty())) {
            std::cerr << "fuzz-smoke: valid DAP frame was not parsed at iteration "
                      << iteration << "\n";
            return false;
        }
        if (iteration % 7 >= 2 && iteration % 7 <= 4 && client.LastError().empty()) {
            std::cerr << "fuzz-smoke: malformed DAP frame was accepted at iteration "
                      << iteration << "\n";
            return false;
        }
    }
    return true;
}

bool ReplayDapCorpus(const wxString& corpusRoot)
{
    if (corpusRoot.empty()) return true;
    constexpr size_t kMaximumSeedSize = 1024U * 1024U;
    constexpr size_t kMaximumCorpusBytes = 8U * 1024U * 1024U;
    constexpr size_t kMaximumSeeds = 64U;
    std::vector<std::pair<std::filesystem::path, bool>> seeds;
    size_t totalBytes = 0;
    for (const auto& category : {std::string("valid"), std::string("invalid")}) {
        const std::filesystem::path directory =
            std::filesystem::path(corpusRoot.ToStdString()) / category;
        if (!std::filesystem::is_directory(directory)) {
            std::cerr << "fuzz-smoke: missing DAP corpus directory " << directory << "\n";
            return false;
        }
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;
            seeds.emplace_back(entry.path(), category == "valid");
        }
    }
    std::sort(seeds.begin(), seeds.end(), [](const auto& left, const auto& right) {
        return left.first.generic_string() < right.first.generic_string();
    });
    if (seeds.empty() || seeds.size() > kMaximumSeeds) {
        std::cerr << "fuzz-smoke: DAP corpus seed count is outside the bounded range\n";
        return false;
    }
    for (const auto& [path, valid] : seeds) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            std::cerr << "fuzz-smoke: could not read DAP corpus seed " << path << "\n";
            return false;
        }
        const std::string bytes((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
        if (bytes.empty() || bytes.size() > kMaximumSeedSize ||
            totalBytes > kMaximumCorpusBytes - bytes.size()) {
            std::cerr << "fuzz-smoke: DAP corpus seed exceeds its bounded size " << path << "\n";
            return false;
        }
        totalBytes += bytes.size();
        codium::DapClient client(nullptr, wxID_HIGHEST + 2200);
        wxArrayString messages;
        for (const char byte : bytes) {
            const wxArrayString parsed = client.ParseBytesForTesting(std::string(1, byte));
            for (const auto& message : parsed) messages.Add(message);
        }
        if (valid) {
            if (messages.empty() || !client.LastError().empty()) {
                std::cerr << "fuzz-smoke: valid DAP corpus seed was rejected: " << path << "\n";
                return false;
            }
        } else if (client.LastError().empty()) {
            std::cerr << "fuzz-smoke: invalid DAP corpus seed was accepted: " << path << "\n";
            return false;
        }
    }
    return true;
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
        left.ScrollbackSize() != right.ScrollbackSize()) return false;

    for (int row = 0; row < left.Rows(); ++row) {
        for (int column = 0; column < left.Columns(); ++column) {
            const auto& a = left.CellAt(column, row);
            const auto& b = right.CellAt(column, row);
            if (a.character != b.character || a.text != b.text || a.foreground != b.foreground ||
                a.background != b.background || a.bold != b.bold || a.underline != b.underline ||
                a.inverse != b.inverse || a.hyperlink != b.hyperlink || a.width != b.width ||
                a.continuation != b.continuation) return false;
        }
    }
    return true;
}

wxString AnsiPayload(std::mt19937& generator)
{
    static const wxString escapes[] = {
        wxS("\x1b[31m"), wxS("\x1b[0m"), wxS("\x1b[2J"), wxS("\x1b[?25l"),
        wxS("\x1b[?1000h"), wxS("\x1b[?1006h"), wxS("\x1b[?2004h"),
        wxS("\x1b[?2026h"), wxS("\x1b[?2026l"),
        wxString::FromUTF8("\x1b]8;;https://example.com\x1b\\"),
        wxString::FromUTF8("\x1bPq1;2;3\x1b\\"), wxS("\r\n")
    };
    wxString result;
    const size_t size = static_cast<size_t>(Next(generator) % 320U);
    for (size_t index = 0; index < size; ++index) {
        if (index % 13 == 0) {
            result += escapes[Next(generator) % (sizeof(escapes) / sizeof(escapes[0]))];
        } else if (index % 17 == 0) {
            result += wxString::FromUTF8("\xE7\x95\x8C");
        } else if (index % 19 == 0) {
            result += wxString::FromUTF8("e\xCC\x81");
        } else {
            result += wxChar(' ' + static_cast<wxChar>(Next(generator) % 95U));
        }
    }
    return result;
}

bool RunAnsiFuzz(int iterations, std::mt19937& generator)
{
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const wxString payload = AnsiPayload(generator);
        codium::TerminalScreen complete(24, 8);
        codium::TerminalScreen fragmented(24, 8);
        complete.Feed(payload);
        size_t offset = 0;
        while (offset < payload.length()) {
            const size_t chunk = std::min(payload.length() - offset,
                                          static_cast<size_t>(1U + Next(generator) % 13U));
            fragmented.Feed(payload.Mid(offset, chunk));
            offset += chunk;
        }
        if (!SameScreen(complete, fragmented) || complete.CursorColumn() < 0 ||
            complete.CursorColumn() > complete.Columns() || complete.CursorRow() < 0 ||
            complete.CursorRow() >= complete.Rows()) {
            std::cerr << "fuzz-smoke: ANSI invariant failed at iteration " << iteration
                      << " seed=0xC0D1\n";
            return false;
        }
    }
    return true;
}

bool CreateFuzzVsix(const wxString& path, int iteration, std::mt19937& generator,
                    bool includeUnsafePath)
{
    wxFFileOutputStream output(path);
    if (!output.IsOk()) return false;
    wxZipOutputStream archive(output);
    archive.PutNextEntry(wxS("extension/package.json"));
    const wxString manifest = wxString::Format(
        wxS("{\"name\":\"fuzz\",\"publisher\":\"codium\",\"version\":\"1.0.%d\"}\n"),
        iteration);
    const wxScopedCharBuffer manifestUtf8 = manifest.utf8_str();
    archive.Write(manifestUtf8.data(), manifestUtf8.length());
    archive.PutNextEntry(wxS("extension/extension.js"));
    const wxString script = wxString::Format(
        wxS("module.exports = { activate() { return %d; } };\n"),
        iteration);
    const wxScopedCharBuffer scriptUtf8 = script.utf8_str();
    archive.Write(scriptUtf8.data(), scriptUtf8.length());

    const int extraEntries = static_cast<int>(Next(generator) % 12U);
    for (int entryIndex = 0; entryIndex < extraEntries; ++entryIndex) {
        const wxString entryName = wxString::Format(
            wxS("extension/files/%04d-%02d.txt"), iteration, entryIndex);
        archive.PutNextEntry(entryName);
        const size_t byteCount = static_cast<size_t>(Next(generator) % 512U);
        std::string content(byteCount, '\0');
        for (char& byte : content) byte = static_cast<char>(Next(generator) & 0xffU);
        archive.Write(content.data(), content.size());
    }

    if (includeUnsafePath) {
        archive.PutNextEntry(wxS("../escape.txt"));
        const char payload[] = "must not be extracted\n";
        archive.Write(payload, sizeof(payload) - 1);
    }
    return archive.Close();
}

bool RunVsixFuzz(const wxString& tempRoot, int iterations, std::mt19937& generator)
{
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const wxString path = tempRoot + wxFILE_SEP_PATH +
            wxString::Format(wxS("archive-%04d.vsix"), iteration);
        // Keep ZIP structure valid so the production policy, rather than a
        // platform-specific third-party stream abort, is the observed result.
        const bool includeUnsafePath = (iteration % 5) == 0;
        if (!CreateFuzzVsix(path, iteration, generator, includeUnsafePath)) return false;

        const wxString installRoot = tempRoot + wxFILE_SEP_PATH +
            wxString::Format(wxS("installed-%04d"), iteration);
        codium::VsixManager manager(installRoot);
        wxString digest;
        wxString message;
        if (!codium::ExtensionSecurity::ComputeSha256(path, &digest, &message)) return false;
        const bool installed = manager.InstallVerified(path, digest, &message);
        if (installed == includeUnsafePath) {
            std::cerr << "fuzz-smoke: VSIX semantic mutation had an unexpected result at iteration "
                      << iteration << "\n";
            return false;
        }
        if (wxFileExists(tempRoot + wxFILE_SEP_PATH + wxS("escape.txt"))) {
            std::cerr << "fuzz-smoke: VSIX extraction escaped its root at iteration "
                      << iteration << "\n";
            return false;
        }
        if (!RemoveTreeNoThrow(installRoot) || !RemoveFileNoThrow(path)) {
            std::cerr << "fuzz-smoke: could not clean VSIX case at iteration "
                      << iteration << "\n";
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "fuzz-smoke: wxWidgets initialization or source root failed\n";
        return 1;
    }

    const wxString tempRoot = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH +
                              wxString::Format(wxS("codium-blocks-fuzz-smoke-%lu"),
                                               wxGetProcessId());
    if (!RemoveTreeNoThrow(tempRoot)) {
        std::cerr << "fuzz-smoke: could not clean the temporary directory\n";
        return 1;
    }
    if (!wxFileName::Mkdir(tempRoot, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
        std::cerr << "fuzz-smoke: could not create temporary directory\n";
        return 1;
    }

    const int iterations = Iterations();
    std::mt19937 generator(kSeed);
    (void)argc;
    (void)argv;
    const wxString corpusRoot = argc > 1 ? wxString::FromUTF8(argv[1]) : wxString();
    const bool ok = ReplayDapCorpus(corpusRoot) &&
                    RunAnsiFuzz(iterations, generator) &&
                    RunDapFuzz(iterations, generator) &&
                    RunVsixFuzz(tempRoot, iterations, generator);
    const bool cleaned = RemoveTreeNoThrow(tempRoot);
    if (!ok) return 1;
    if (!cleaned) {
        std::cerr << "fuzz-smoke: could not clean the temporary directory\n";
        return 1;
    }
    std::cout << "fuzz-smoke: ok — bounded DAP, ANSI, and VSIX fuzzing; seed=0xC0D1 iterations="
              << iterations << "\n";
    return 0;
}
