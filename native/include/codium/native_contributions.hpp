// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <map>

namespace codium {

struct TreeViewItem final {
    wxString id;
    wxString label;
};

class TreeViewRegistry final {
public:
    void Register(const wxString& viewId, const wxString& title);
    void SetItems(const wxString& viewId, const wxArrayString& labels);
    wxArrayString ViewTitles() const;
    wxArrayString Items(const wxString& viewId) const;

private:
    std::map<wxString, wxString> titles_;
    std::map<wxString, wxArrayString> items_;
};

class ScmModel final {
public:
    bool Refresh(const wxString& rootPath, wxString* error = nullptr);
    bool IsRepository() const { return repository_; }
    const wxArrayString& Resources() const { return resources_; }

private:
    bool repository_ = false;
    wxArrayString resources_;
};

class CustomEditorRegistry final {
public:
    void Register(const wxString& extension, const wxString& editorId);
    wxString Resolve(const wxString& path) const;
    wxArrayString Entries() const;

private:
    std::map<wxString, wxString> editors_;
};

} // namespace codium
