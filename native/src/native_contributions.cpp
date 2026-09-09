#include "codium/native_contributions.hpp"

#include <wx/filename.h>
#include <wx/utils.h>

namespace codium {

namespace {

wxString QuoteArgument(const wxString& value)
{
    wxString escaped = value;
    escaped.Replace(wxS("\""), wxS("\\\""));
    return wxS("\"") + escaped + wxS("\"");
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

bool ScmModel::Refresh(const wxString& rootPath, wxString* error)
{
    repository_ = wxDirExists(rootPath + wxFILE_SEP_PATH + wxS(".git")) ||
                  wxFileExists(rootPath + wxFILE_SEP_PATH + wxS(".git"));
    resources_.Clear();
    if (!repository_) {
        if (error) *error = wxS("The workspace is not a Git repository.");
        return false;
    }
    const wxString command = QuoteArgument(wxS("git")) + wxS(" ") + QuoteArgument(wxS("-C")) + wxS(" ") +
                             QuoteArgument(rootPath) + wxS(" ") + QuoteArgument(wxS("status")) + wxS(" ") +
                             QuoteArgument(wxS("--porcelain"));
    wxArrayString output;
    wxArrayString errors;
    const long status = wxExecute(command, output, errors, wxEXEC_SYNC);
    if (status != 0) {
        if (error) *error = errors.IsEmpty() ? wxS("git status failed.") : errors[0];
        return false;
    }
    for (const auto& line : output) {
        if (!line.empty()) resources_.Add(line);
    }
    if (resources_.IsEmpty()) resources_.Add(wxS("Working tree clean"));
    return true;
}

void CustomEditorRegistry::Register(const wxString& extension, const wxString& editorId)
{
    wxString normalized = extension.Lower();
    if (normalized.StartsWith(wxS("."))) normalized = normalized.Mid(1);
    if (!normalized.empty() && !editorId.empty()) editors_[normalized] = editorId;
}

wxString CustomEditorRegistry::Resolve(const wxString& path) const
{
    const wxString extension = wxFileName(path).GetExt().Lower();
    const auto found = editors_.find(extension);
    return found == editors_.end() ? wxString(wxEmptyString) : found->second;
}

wxArrayString CustomEditorRegistry::Entries() const
{
    wxArrayString result;
    for (const auto& [extension, editor] : editors_) result.Add(wxS(".") + extension + wxS(" -> ") + editor);
    return result;
}

} // namespace codium
