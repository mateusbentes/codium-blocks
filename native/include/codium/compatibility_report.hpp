#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

namespace codium {

struct CompatibilityReport final {
    bool manifestValid = false;
    bool activationSupported = false;
    wxString extensionId;
    wxArrayString supportedFeatures;
    wxArrayString unsupportedFeatures;
    wxArrayString warnings;
};

class CompatibilityReporter final {
public:
    static CompatibilityReport Analyze(const wxString& manifestJson);
};

} // namespace codium
