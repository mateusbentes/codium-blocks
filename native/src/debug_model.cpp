#include "codium/debug_model.hpp"

#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>

#include <functional>
#include <iomanip>
#include <sstream>

namespace codium {

namespace {

wxString JsonEscape(const wxString& value)
{
    wxString result;
    for (const auto character : value) {
        if (character == wxChar('\\')) result += wxS("\\\\");
        else if (character == wxChar('"')) result += wxS("\\\"");
        else result += character;
    }
    return result;
}

wxString Normalize(wxString value)
{
    value.Replace(wxS("\\"), wxS("/"));
    while (value.length() > 1 && value.EndsWith(wxS("/"))) value.RemoveLast();
    return value;
}

wxString WatchFilePath(const wxString& workspaceRoot)
{
    wxString dataRoot;
    if (!wxGetEnv(wxS("CODIUM_BLOCKS_DATA"), &dataRoot) || dataRoot.empty()) {
        dataRoot = wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + wxS("CodiumBlocks");
    }
    const std::string keyInput = workspaceRoot.utf8_str().data();
    const size_t key = std::hash<std::string>{}(keyInput);
    std::ostringstream suffix;
    suffix << std::hex << key;
    const wxString directory = dataRoot + wxFILE_SEP_PATH + wxS("watches");
    wxFileName::Mkdir(directory, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return directory + wxFILE_SEP_PATH + wxString::FromUTF8(suffix.str()) + wxS(".txt");
}

} // namespace

void SourceMapper::Add(const wxString& remoteRoot, const wxString& localRoot)
{
    const wxString remote = Normalize(remoteRoot);
    const wxString local = Normalize(localRoot);
    if (!remote.empty() && !local.empty()) mappings_[remote] = local;
}

void SourceMapper::Clear()
{
    mappings_.clear();
}

wxString SourceMapper::Map(const wxString& remotePath) const
{
    const wxString normalized = Normalize(remotePath);
    size_t bestLength = 0;
    wxString bestRemote;
    wxString bestLocal;
    for (const auto& [remote, local] : mappings_) {
        if ((normalized == remote || normalized.StartsWith(remote + wxS("/"))) && remote.length() > bestLength) {
            bestLength = remote.length();
            bestRemote = remote;
            bestLocal = local;
        }
    }
    if (bestRemote.empty()) return remotePath;
    wxString suffix = normalized.Mid(bestRemote.length());
    return bestLocal + suffix;
}

wxString SourceMapper::ToJson() const
{
    wxString result = wxS("{");
    bool first = true;
    for (const auto& [remote, local] : mappings_) {
        if (!first) result += wxS(",");
        first = false;
        result += wxS("\"") + JsonEscape(remote) + wxS("\":\"") + JsonEscape(local) + wxS("\"");
    }
    return result + wxS("}");
}

wxArrayString WatchStore::Load(const wxString& workspaceRoot)
{
    wxArrayString expressions;
    const wxString path = WatchFilePath(workspaceRoot);
    if (!wxFileExists(path)) return expressions;
    wxFile file;
    if (!file.Open(path, wxFile::read)) return expressions;
    wxString text;
    if (!file.ReadAll(&text)) return expressions;
    wxStringTokenizer tokenizer(text, wxS("\n"), wxTOKEN_STRTOK);
    while (tokenizer.HasMoreTokens()) {
        const wxString expression = tokenizer.GetNextToken().Trim(true).Trim(false);
        if (!expression.empty()) expressions.Add(expression);
    }
    return expressions;
}

bool WatchStore::Save(const wxString& workspaceRoot, const wxArrayString& expressions, wxString* error)
{
    const wxString path = WatchFilePath(workspaceRoot);
    const wxString temporary = path + wxS(".tmp");
    wxFile file;
    if (!file.Open(temporary, wxFile::write)) {
        if (error) *error = wxString::Format(wxS("Could not write watches: %s."), temporary);
        return false;
    }
    wxString text;
    for (const auto& expression : expressions) text += expression + wxS("\n");
    const wxScopedCharBuffer bytes = text.utf8_str();
    if (file.Write(bytes.data(), bytes.length()) != bytes.length() || !file.Close() || !wxRenameFile(temporary, path, true)) {
        wxRemoveFile(temporary);
        if (error) *error = wxString::Format(wxS("Could not commit watches: %s."), path);
        return false;
    }
    return true;
}

} // namespace codium
