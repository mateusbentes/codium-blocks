#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

namespace codium {

struct TerminalProfile final {
    wxString shell;
    int columns = 120;
    int rows = 32;
    wxArrayString history;
};

class TerminalProfileStore final {
public:
    static TerminalProfile Load(const wxString& profileName = wxS("default"));
    static bool Save(const TerminalProfile& profile, const wxString& profileName = wxS("default"),
                     wxString* error = nullptr);
    static wxString ConfigPath();
};

} // namespace codium
