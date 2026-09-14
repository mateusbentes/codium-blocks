// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <map>
#include <vector>

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

enum class ScmChangeKind {
    Added,
    Modified,
    Deleted,
    Renamed,
    Untracked,
    Conflicted,
    Unknown,
};

struct ScmResource final {
    wxString path;
    wxString originalPath;
    ScmChangeKind kind = ScmChangeKind::Unknown;
    bool staged = false;
    bool worktree = false;
};

class ScmModel final {
public:
    bool Refresh(const wxString& rootPath, wxString* error = nullptr);
    bool IsRepository() const { return repository_; }
    const wxArrayString& Resources() const { return resources_; }
    const std::vector<ScmResource>& ChangeList() const { return changes_; }
    bool Stage(const wxString& path, wxString* error = nullptr);
    bool Unstage(const wxString& path, wxString* error = nullptr);
    bool Discard(const wxString& path, wxString* error = nullptr);
    static wxString ChangeKindName(ScmChangeKind kind);

private:
    bool RunGit(const wxString& rootPath, const wxArrayString& arguments,
                wxArrayString* output, wxString* error) const;
    wxString rootPath_;
    bool repository_ = false;
    wxArrayString resources_;
    std::vector<ScmResource> changes_;
};

struct CustomEditorDescriptor final {
    wxString extension;
    wxString editorId;
    wxString label;
    int priority = 0;
    bool supportsText = false;
};

class CustomEditorRegistry final {
public:
    void Register(const wxString& extension, const wxString& editorId,
                  const wxString& label = wxEmptyString, int priority = 0,
                  bool supportsText = false);
    wxString Resolve(const wxString& path) const;
    const CustomEditorDescriptor* ResolveDescriptor(const wxString& path) const;
    std::vector<CustomEditorDescriptor> Descriptors() const;
    wxArrayString Entries() const;

private:
    std::map<wxString, CustomEditorDescriptor> editors_;
};

class WebviewResourcePolicy final {
public:
    static bool IsAllowedUri(const wxString& uri, const wxString& extensionRoot);
    static wxString SanitizeHtml(const wxString& html);
};

} // namespace codium
