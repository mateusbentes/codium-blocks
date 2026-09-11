// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#include "codium/localization.hpp"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/strconv.h>
#include <wx/textfile.h>
#include <wx/utils.h>

#include <algorithm>

namespace codium {

namespace {

wxString NormalizeLanguageName(wxString value)
{
    value = value.Lower();
    value.Replace(wxS("_"), wxS("-"));
    const int separator = value.Find(wxS('.'));
    if (separator != wxNOT_FOUND) value = value.Left(separator);
    return value;
}

wxString Trimmed(wxString value)
{
    return value.Trim(true).Trim(false);
}

bool IsPortuguese(const wxString& value)
{
    const wxString normalized = NormalizeLanguageName(value);
    return normalized == wxS("pt") || normalized.StartsWith(wxS("pt-"));
}

} // namespace

bool Localization::Load(const wxString& resourceRoot, const wxString& dataRoot, wxString* error)
{
    resourceRoot_ = resourceRoot;
    dataRoot_ = dataRoot;
    environmentOverride_ = false;
    wxFileName::Mkdir(dataRoot_, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    selectedLanguage_ = UiLanguage::System;
    const wxString preference = PreferencePath();
    if (wxFileExists(preference)) {
        wxTextFile file;
        if (file.Open(preference)) {
            for (size_t index = 0; index < file.GetLineCount(); ++index) {
                const wxString line = Trimmed(file.GetLine(index));
                if (line.StartsWith(wxS("language="))) {
                    selectedLanguage_ = LanguageFromName(line.Mid(9));
                    break;
                }
            }
            file.Close();
        }
    }

    wxString configured;
    if (wxGetEnv(wxS("CODIUM_BLOCKS_LANGUAGE"), &configured) && !configured.empty()) {
        selectedLanguage_ = LanguageFromName(configured);
        environmentOverride_ = true;
    }
    return SetLanguage(selectedLanguage_, error);
}

bool Localization::SetLanguage(UiLanguage language, wxString* error)
{
    selectedLanguage_ = language;
    activeLanguage_ = language == UiLanguage::System ? DetectSystemLanguage() : language;
    activeCode_ = LanguageCode(activeLanguage_);

    std::map<wxString, wxString> english;
    wxString englishError;
    if (!LoadCatalog(CatalogPath(wxS("en-US")), &english, &englishError)) {
        if (error) *error = englishError;
        return false;
    }

    catalog_ = english;
    if (activeLanguage_ != UiLanguage::English) {
        std::map<wxString, wxString> translated;
        wxString translatedError;
        if (LoadCatalog(CatalogPath(activeCode_), &translated, &translatedError)) {
            for (const auto& entry : translated) catalog_[entry.first] = entry.second;
        } else if (error) {
            *error = wxString::Format(wxS("Localization catalog %s unavailable; English fallback is active."), activeCode_);
        }
    }

    return environmentOverride_ ? true : SavePreference(error);
}

wxString Localization::Text(const wxString& key) const
{
    const auto found = catalog_.find(key);
    return found == catalog_.end() ? key : found->second;
}

wxString Localization::Text(const wxString& key, const wxString& fallback) const
{
    const auto found = catalog_.find(key);
    return found == catalog_.end() ? fallback : found->second;
}

UiLanguage Localization::LanguageFromName(const wxString& name)
{
    const wxString normalized = NormalizeLanguageName(name);
    if (normalized == wxS("en") || normalized == wxS("en-us") || normalized == wxS("english")) {
        return UiLanguage::English;
    }
    if (IsPortuguese(normalized) || normalized == wxS("pt-br") || normalized == wxS("portuguese")) {
        return UiLanguage::PortugueseBrazil;
    }
    return UiLanguage::System;
}

wxString Localization::LanguageCode(UiLanguage language)
{
    switch (language) {
    case UiLanguage::English: return wxS("en-US");
    case UiLanguage::PortugueseBrazil: return wxS("pt-BR");
    case UiLanguage::System: return wxS("system");
    }
    return wxS("en-US");
}

wxString Localization::LanguageName(UiLanguage language)
{
    switch (language) {
    case UiLanguage::English: return wxS("English");
    case UiLanguage::PortugueseBrazil: return wxS("Português (Brasil)");
    case UiLanguage::System: return wxS("System default");
    }
    return wxS("System default");
}

UiLanguage Localization::DetectSystemLanguage()
{
    wxString value;
    const wxString variables[] = {wxS("LC_ALL"), wxS("LC_MESSAGES"), wxS("LANG")};
    for (const auto& variable : variables) {
        if (wxGetEnv(variable, &value) && !value.empty()) {
            return IsPortuguese(value) ? UiLanguage::PortugueseBrazil : UiLanguage::English;
        }
    }

    const int systemLanguage = wxLocale::GetSystemLanguage();
    const wxString canonical = wxLocale::GetLanguageCanonicalName(systemLanguage);
    return IsPortuguese(canonical) ? UiLanguage::PortugueseBrazil : UiLanguage::English;
}

bool Localization::LoadCatalog(const wxString& path, std::map<wxString, wxString>* catalog, wxString* error) const
{
    if (!catalog) return false;
    wxTextFile file;
    if (!file.Open(path, wxConvUTF8)) {
        if (error) *error = wxString::Format(wxS("Could not open localization catalog: %s"), path);
        return false;
    }

    catalog->clear();
    for (size_t index = 0; index < file.GetLineCount(); ++index) {
        const wxString line = file.GetLine(index);
        const wxString trimmed = Trimmed(line);
        if (trimmed.empty() || trimmed.StartsWith(wxS("#"))) continue;
        const int separator = line.Find(wxChar('\t'));
        if (separator == wxNOT_FOUND) continue;
        const wxString key = Trimmed(line.Left(separator));
        wxString value = line.Mid(separator + 1);
        value.Replace(wxS("\\s"), wxS(" "));
        if (!key.empty()) (*catalog)[key] = value;
    }
    file.Close();
    return true;
}

bool Localization::SavePreference(wxString* error) const
{
    if (dataRoot_.empty()) return true;
    wxFileName::Mkdir(dataRoot_, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const wxString path = PreferencePath();
    const wxString temporary = path + wxS(".tmp");
    wxFile file(temporary, wxFile::write);
    if (!file.IsOpened()) {
        if (error) *error = wxString::Format(wxS("Could not save language preference: %s"), path);
        return false;
    }
    const wxString content = wxString::Format(wxS("# Codium::Blocks UI language preference\nlanguage=%s\n"),
                                               LanguageCode(selectedLanguage_));
    const wxScopedCharBuffer utf8 = content.utf8_str();
    file.Write(utf8.data(), utf8.length());
    file.Close();
    if (!wxRenameFile(temporary, path, true)) {
        if (error) *error = wxString::Format(wxS("Could not replace language preference: %s"), path);
        return false;
    }
    return true;
}

wxString Localization::PreferencePath() const
{
    return dataRoot_ + wxFILE_SEP_PATH + wxS("ui-language.tsv");
}

wxString Localization::CatalogPath(const wxString& code) const
{
    return resourceRoot_ + wxFILE_SEP_PATH + wxS("locales") + wxFILE_SEP_PATH + code + wxS(".tsv");
}

} // namespace codium
