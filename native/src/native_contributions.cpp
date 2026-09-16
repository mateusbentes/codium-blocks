// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/native_contributions.hpp"

#include <wx/filename.h>
#include <wx/regex.h>
#include <wx/utils.h>

#include <algorithm>
#include <vector>

namespace codium {

namespace {

bool IsWithin(const wxString& candidate, const wxString& root);

wxString QuoteArgument(const wxString& value)
{
#if defined(__WXMSW__)
    // wxExecute receives one command line on Windows. Backslashes are only
    // special immediately before a quote or at the end of a quoted argument;
    // doubling every path separator produces invalid paths for some Git
    // builds on the hosted runner.
    wxString escaped;
    size_t backslashes = 0;
    for (const wxUniChar character : value) {
        if (character == wxUniChar('\\')) {
            ++backslashes;
            continue;
        }
        if (character == wxUniChar('"')) {
            escaped.Append(wxUniChar('\\'), backslashes * 2 + 1);
            escaped += character;
        } else {
            escaped.Append(wxUniChar('\\'), backslashes);
            escaped += character;
        }
        backslashes = 0;
    }
    escaped.Append(wxUniChar('\\'), backslashes * 2);
    return wxS("\"") + escaped + wxS("\"");
#else
    wxString escaped = value;
    escaped.Replace(wxS("'"), wxS("'\\''"));
    return wxS("'") + escaped + wxS("'");
#endif
}

bool IsSafeRepositoryPath(const wxString& path, const wxString& root)
{
    if (path.empty()) return false;
    wxFileName candidate(path);
    if (!candidate.IsAbsolute()) candidate.Assign(root, path);
    return IsWithin(candidate.GetFullPath(), wxFileName(root).GetFullPath());
}

ScmChangeKind KindForStatus(wxChar index, wxChar worktree)
{
    if (index == wxChar('?') && worktree == wxChar('?')) return ScmChangeKind::Untracked;
    if (index == wxChar('U') || worktree == wxChar('U') ||
        (index == wxChar('A') && worktree == wxChar('A'))) return ScmChangeKind::Conflicted;
    if (index == wxChar('R') || worktree == wxChar('R')) return ScmChangeKind::Renamed;
    if (index == wxChar('A') || worktree == wxChar('A')) return ScmChangeKind::Added;
    if (index == wxChar('D') || worktree == wxChar('D')) return ScmChangeKind::Deleted;
    if (index == wxChar('M') || worktree == wxChar('M')) return ScmChangeKind::Modified;
    return ScmChangeKind::Unknown;
}

bool IsWithin(const wxString& candidate, const wxString& root)
{
    const wxString normalizedCandidate = wxFileName(candidate).GetFullPath();
    const wxString normalizedRoot = wxFileName(root).GetFullPath();
    return normalizedCandidate == normalizedRoot ||
           normalizedCandidate.StartsWith(normalizedRoot + wxFILE_SEP_PATH);
}

} // namespace

void TreeViewRegistry::Register(const wxString& viewId, const wxString& title)
{
    if (!viewId.empty() && !title.empty()) titles_[viewId] = title;
}

void TreeViewRegistry::SetItems(const wxString& viewId, const wxArrayString& labels)
{
    if (titles_.find(viewId) != titles_.end()) items_[viewId] = labels;
}

wxArrayString TreeViewRegistry::ViewTitles() const
{
    wxArrayString result;
    for (const auto& [id, title] : titles_) result.Add(title);
    return result;
}

wxArrayString TreeViewRegistry::Items(const wxString& viewId) const
{
    const auto found = items_.find(viewId);
    return found == items_.end() ? wxArrayString{} : found->second;
}

bool ScmModel::RunGit(const wxString& rootPath, const wxArrayString& arguments,
                      wxArrayString* output, wxString* error) const
{
    wxString command = QuoteArgument(wxS("git"));
#if defined(__WXMSW__)
    wxExecuteEnv environment;
    environment.cwd = rootPath;
#else
    command += wxS(" ") + QuoteArgument(wxS("-C")) + wxS(" ") + QuoteArgument(rootPath);
#endif
    for (const auto& argument : arguments) command += wxS(" ") + QuoteArgument(argument);
    wxArrayString errors;
    wxArrayString localOutput;
    wxArrayString& captured = output ? *output : localOutput;
#if defined(__WXMSW__)
    const long status = wxExecute(command, captured, errors, wxEXEC_SYNC, &environment);
#else
    const long status = wxExecute(command, captured, errors, wxEXEC_SYNC);
#endif
    if (status != 0) {
        if (error) *error = errors.IsEmpty() ? wxS("Git command failed.") : errors[0];
        return false;
    }
    return true;
}

bool ScmModel::Refresh(const wxString& rootPath, wxString* error)
{
    rootPath_ = rootPath;
    repository_ = wxDirExists(rootPath + wxFILE_SEP_PATH + wxS(".git")) ||
                  wxFileExists(rootPath + wxFILE_SEP_PATH + wxS(".git"));
    resources_.Clear();
    changes_.clear();
    if (!repository_) {
        if (error) *error = wxS("The workspace is not a Git repository.");
        return false;
    }

    wxArrayString output;
    if (!RunGit(rootPath, wxArrayString{wxS("status"), wxS("--porcelain=v1")}, &output, error)) return false;
    for (const auto& line : output) {
        if (line.length() < 3) continue;
        const wxChar index = line[0];
        const wxChar worktree = line[1];
        wxString path = line.Mid(3);
        wxString originalPath;
        const int renameSeparator = path.Find(wxS(" -> "));
        if (renameSeparator != wxNOT_FOUND) {
            originalPath = path.Left(renameSeparator);
            path = path.Mid(renameSeparator + 4);
        }
        ScmResource resource;
        resource.path = path;
        resource.originalPath = originalPath;
        resource.kind = KindForStatus(index, worktree);
        resource.staged = index != wxChar(' ') && index != wxChar('?');
        resource.worktree = worktree != wxChar(' ') && worktree != wxChar('?');
        changes_.push_back(resource);
        resources_.Add(wxString::Format(wxS("%s %s"), ChangeKindName(resource.kind), path));
    }
    if (resources_.IsEmpty()) resources_.Add(wxS("Working tree clean"));
    return true;
}

bool ScmModel::Stage(const wxString& path, wxString* error)
{
    if (!IsSafeRepositoryPath(path, rootPath_)) {
        if (error) *error = wxS("SCM path must remain inside the workspace repository.");
        return false;
    }
    wxArrayString arguments;
    arguments.Add(wxS("add"));
    arguments.Add(wxS("--"));
    arguments.Add(path);
    return RunGit(rootPath_, arguments, nullptr, error);
}

bool ScmModel::Unstage(const wxString& path, wxString* error)
{
    if (!IsSafeRepositoryPath(path, rootPath_)) {
        if (error) *error = wxS("SCM path must remain inside the workspace repository.");
        return false;
    }
    wxArrayString arguments;
    arguments.Add(wxS("restore"));
    arguments.Add(wxS("--staged"));
    arguments.Add(wxS("--"));
    arguments.Add(path);
    return RunGit(rootPath_, arguments, nullptr, error);
}

bool ScmModel::Discard(const wxString& path, wxString* error)
{
    if (!IsSafeRepositoryPath(path, rootPath_)) {
        if (error) *error = wxS("SCM path must remain inside the workspace repository.");
        return false;
    }
    wxArrayString arguments;
    arguments.Add(wxS("restore"));
    arguments.Add(wxS("--"));
    arguments.Add(path);
    return RunGit(rootPath_, arguments, nullptr, error);
}

wxString ScmModel::ChangeKindName(ScmChangeKind kind)
{
    switch (kind) {
    case ScmChangeKind::Added: return wxS("added");
    case ScmChangeKind::Modified: return wxS("modified");
    case ScmChangeKind::Deleted: return wxS("deleted");
    case ScmChangeKind::Renamed: return wxS("renamed");
    case ScmChangeKind::Untracked: return wxS("untracked");
    case ScmChangeKind::Conflicted: return wxS("conflicted");
    default: return wxS("unknown");
    }
}

void CustomEditorRegistry::Register(const wxString& extension, const wxString& editorId,
                                    const wxString& label, int priority, bool supportsText)
{
    wxString normalized = extension.Lower();
    if (normalized.StartsWith(wxS("."))) normalized = normalized.Mid(1);
    if (!normalized.empty() && !editorId.empty()) {
        editors_[normalized] = CustomEditorDescriptor{
            wxS(".") + normalized, editorId, label.empty() ? editorId : label, priority, supportsText};
    }
}

wxString CustomEditorRegistry::Resolve(const wxString& path) const
{
    const auto* descriptor = ResolveDescriptor(path);
    return descriptor ? descriptor->editorId : wxString(wxEmptyString);
}

const CustomEditorDescriptor* CustomEditorRegistry::ResolveDescriptor(const wxString& path) const
{
    const wxString extension = wxFileName(path).GetExt().Lower();
    const auto found = editors_.find(extension);
    return found == editors_.end() ? nullptr : &found->second;
}

std::vector<CustomEditorDescriptor> CustomEditorRegistry::Descriptors() const
{
    std::vector<CustomEditorDescriptor> result;
    for (const auto& [extension, descriptor] : editors_) result.push_back(descriptor);
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.priority != right.priority) return left.priority > right.priority;
        return left.extension < right.extension;
    });
    return result;
}

wxArrayString CustomEditorRegistry::Entries() const
{
    wxArrayString result;
    for (const auto& descriptor : Descriptors()) {
        result.Add(wxString::Format(wxS("%s -> %s (%s)"), descriptor.extension,
                                    descriptor.editorId, descriptor.label));
    }
    return result;
}

bool WebviewResourcePolicy::IsAllowedUri(const wxString& uri, const wxString& extensionRoot)
{
    if (uri.StartsWith(wxS("data:")) || uri.StartsWith(wxS("blob:"))) return false;
    if (uri.StartsWith(wxS("https://")) || uri.StartsWith(wxS("http://"))) return false;
    if (!uri.StartsWith(wxS("file://"))) return false;
    const wxString path = uri.Mid(7);
    return IsWithin(path, extensionRoot);
}

wxString WebviewResourcePolicy::SanitizeHtml(const wxString& html)
{
    wxString sanitized = html;
    const wxString lower = sanitized.Lower();
    for (const auto& tag : {wxString(wxS("<script")), wxString(wxS("<iframe")), wxString(wxS("<object")), wxString(wxS("<embed"))}) {
        int position = lower.Find(tag);
        while (position != wxNOT_FOUND) {
            const int end = sanitized.Mid(position).Find(wxChar('>'));
            if (end == wxNOT_FOUND) {
                sanitized = sanitized.Left(position);
                break;
            }
            sanitized.Remove(static_cast<size_t>(position), static_cast<size_t>(end + 1));
            position = sanitized.Lower().Find(tag);
        }
    }
    wxRegEx eventAttribute(wxS("[[:space:]]+on[a-zA-Z]+[[:space:]]*=[[:space:]]*(\"[^\"]*\"|'[^']*'|[^[:space:]>]+)"), wxRE_ICASE);
    eventAttribute.ReplaceAll(&sanitized, wxS(" data-blocked-event=\"removed\""));
    wxRegEx javascriptScheme(wxS("javascript[[:space:]]*:"), wxRE_ICASE);
    javascriptScheme.ReplaceAll(&sanitized, wxS("blocked:"));
    return sanitized;
}

} // namespace codium
