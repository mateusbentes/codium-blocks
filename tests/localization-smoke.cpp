// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#include "codium/localization.hpp"

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/utils.h>

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

int Fail(const char* message)
{
    std::cerr << "localization-smoke: " << message << '\n';
    return 1;
}

std::vector<std::string> Placeholders(const std::string& text)
{
    std::vector<std::string> result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') continue;
        if (i + 1 < text.size() && text[i + 1] == '%') {
            ++i;
            continue;
        }
        const size_t start = i++;
        while (i < text.size() && (text[i] == '+' || text[i] == '-' ||
                                   (text[i] >= '0' && text[i] <= '9'))) {
            ++i;
        }
        if (i < text.size() && (text[i] == 'z' || text[i] == 'l')) ++i;
        if (i < text.size() && (text[i] == 's' || text[i] == 'd' || text[i] == 'u' || text[i] == 'f')) {
            result.push_back(text.substr(start, i - start + 1));
        }
    }
    return result;
}

bool ReadCatalog(const wxString& path, std::map<std::string, std::string>* catalog)
{
    std::ifstream input(path.ToStdString(), std::ios::binary);
    if (!input) return false;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        const size_t separator = line.find('\t');
        if (separator == std::string::npos || separator == 0) return false;
        const std::string key = line.substr(0, separator);
        if (!catalog->emplace(key, line.substr(separator + 1)).second) return false;
    }
    return true;
}

bool ValidateCatalogs(const wxString& sourceRoot)
{
    const wxString localeRoot = sourceRoot + wxFILE_SEP_PATH + wxS("locales") + wxFILE_SEP_PATH;
    std::map<std::string, std::string> english;
    std::map<std::string, std::string> portuguese;
    if (!ReadCatalog(localeRoot + wxS("en-US.tsv"), &english) ||
        !ReadCatalog(localeRoot + wxS("pt-BR.tsv"), &portuguese)) return false;
    if (english.size() != portuguese.size()) return false;
    for (const auto& [key, englishText] : english) {
        const auto translated = portuguese.find(key);
        if (translated == portuguese.end()) return false;
        if (Placeholders(englishText) != Placeholders(translated->second)) return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return Fail("wxWidgets initialization failed");
    if (argc != 2) return Fail("expected source root argument");

    const wxString sourceRoot = wxString::FromUTF8(argv[1]);
    if (!ValidateCatalogs(sourceRoot)) return Fail("catalog parity or placeholder validation failed");

    const wxString dataRoot = wxFileName::GetTempDir() + wxFILE_SEP_PATH + wxS("codium-blocks-localization-smoke");
    wxFileName::Rmdir(dataRoot, wxPATH_RMDIR_RECURSIVE);
    wxFileName::Mkdir(dataRoot, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    codium::Localization localization;
    wxString error;
    if (!localization.Load(sourceRoot, dataRoot, &error)) return Fail("English catalog did not load");
    if (localization.ActiveLanguage() != codium::UiLanguage::English) return Fail("default language was not English");
    if (localization.Text(wxS("menu.file")) != wxS("&File")) return Fail("English menu fallback failed");
    if (localization.Text(wxS("unknown.key")) != wxS("unknown.key")) return Fail("unknown key fallback failed");

    if (!localization.SetLanguage(codium::UiLanguage::PortugueseBrazil, &error)) return Fail("Portuguese catalog did not load");
    if (localization.Text(wxS("menu.file")) != wxS("&Arquivo")) return Fail("Portuguese menu translation failed");
    if (localization.Text(wxS("about.body")) != wxS("Codium::Blocks")) return Fail("invariant product name changed");
    if (localization.Text(wxS("status.ready")) != wxS("Pronto")) return Fail("Portuguese status translation failed");

    codium::Localization reloaded;
    if (!reloaded.Load(sourceRoot, dataRoot, &error)) return Fail("reloading persisted language failed");
    if (reloaded.SelectedLanguage() != codium::UiLanguage::PortugueseBrazil) return Fail("language preference was not persisted");
    if (reloaded.Text(wxS("menu.file")) != wxS("&Arquivo")) return Fail("persisted Portuguese translation failed");

    if (!reloaded.SetLanguage(codium::UiLanguage::English, &error)) return Fail("English selection failed");
    if (reloaded.Text(wxS("menu.file")) != wxS("&File")) return Fail("English selection did not restore catalog");

    wxSetEnv(wxS("CODIUM_BLOCKS_LANGUAGE"), wxS("pt-BR"));
    codium::Localization overridden;
    if (!overridden.Load(sourceRoot, dataRoot, &error)) return Fail("language environment override failed to load");
    wxUnsetEnv(wxS("CODIUM_BLOCKS_LANGUAGE"));
    if (overridden.ActiveLanguage() != codium::UiLanguage::PortugueseBrazil) return Fail("language environment override was ignored");
    if (overridden.Text(wxS("menu.file")) != wxS("&Arquivo")) return Fail("language environment override translation failed");

    codium::Localization afterOverride;
    if (!afterOverride.Load(sourceRoot, dataRoot, &error)) return Fail("loading after environment override failed");
    if (afterOverride.SelectedLanguage() != codium::UiLanguage::English) return Fail("environment override changed persisted preference");

    wxFileName::Rmdir(dataRoot, wxPATH_RMDIR_RECURSIVE);
    std::cout << "localization-smoke: ok\n";
    return 0;
}
