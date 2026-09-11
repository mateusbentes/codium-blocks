// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#pragma once

#include <wx/string.h>

#include <map>

namespace codium {

enum class UiLanguage {
    System,
    English,
    PortugueseBrazil,
};

class Localization final {
public:
    bool Load(const wxString& resourceRoot, const wxString& dataRoot, wxString* error = nullptr);
    bool SetLanguage(UiLanguage language, wxString* error = nullptr);

    wxString Text(const wxString& key) const;
    wxString Text(const wxString& key, const wxString& fallback) const;

    UiLanguage SelectedLanguage() const { return selectedLanguage_; }
    UiLanguage ActiveLanguage() const { return activeLanguage_; }
    const wxString& ActiveCode() const { return activeCode_; }
    const wxString& ResourceRoot() const { return resourceRoot_; }

    static UiLanguage LanguageFromName(const wxString& name);
    static wxString LanguageCode(UiLanguage language);
    static wxString LanguageName(UiLanguage language);
    static UiLanguage DetectSystemLanguage();

private:
    bool LoadCatalog(const wxString& path, std::map<wxString, wxString>* catalog, wxString* error) const;
    bool SavePreference(wxString* error) const;
    wxString PreferencePath() const;
    wxString CatalogPath(const wxString& code) const;

    wxString resourceRoot_;
    wxString dataRoot_;
    wxString activeCode_ = wxS("en-US");
    std::map<wxString, wxString> catalog_;
    UiLanguage selectedLanguage_ = UiLanguage::System;
    UiLanguage activeLanguage_ = UiLanguage::English;
    bool environmentOverride_ = false;
};

} // namespace codium
