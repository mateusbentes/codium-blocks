#include "codium/compatibility_report.hpp"

#include <wx/init.h>

#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return 1;
    const wxString manifest = wxS("{\"name\":\"demo\",\"publisher\":\"test\",\"version\":\"1.0.0\",\"main\":\"./extension.js\",\"contributes\":{\"commands\":[],\"languages\":[]}}");
    const codium::CompatibilityReport report = codium::CompatibilityReporter::Analyze(manifest);
    if (!report.manifestValid || !report.activationSupported || report.extensionId != wxS("test.demo") ||
        report.supportedFeatures.Index(wxS("commands")) == wxNOT_FOUND) {
        std::cerr << "compatibility-smoke: supported manifest report failed\n";
        return 2;
    }
    const auto unsupported = codium::CompatibilityReporter::Analyze(
        wxS("{\"name\":\"browser-demo\",\"publisher\":\"test\",\"version\":\"1.0.0\",\"browser\":\"./browser.js\",\"enableProposedApi\":true,\"contributes\":{\"webviews\":{}}}"));
    if (!unsupported.manifestValid || unsupported.activationSupported || unsupported.unsupportedFeatures.IsEmpty() || unsupported.warnings.IsEmpty()) {
        std::cerr << "compatibility-smoke: unsupported manifest report failed\n";
        return 3;
    }
    std::cout << "compatibility-smoke: ok — manifest compatibility report\n";
    return 0;
}
