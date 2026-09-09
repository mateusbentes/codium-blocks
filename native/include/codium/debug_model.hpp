#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <map>

namespace codium {

class SourceMapper final {
public:
    void Add(const wxString& remoteRoot, const wxString& localRoot);
    void Clear();
    wxString Map(const wxString& remotePath) const;
    wxString ToJson() const;
    size_t Size() const { return mappings_.size(); }

private:
    std::map<wxString, wxString> mappings_;
};

class WatchStore final {
public:
    static wxArrayString Load(const wxString& workspaceRoot);
    static bool Save(const wxString& workspaceRoot, const wxArrayString& expressions, wxString* error = nullptr);
};

} // namespace codium
