// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/compatibility_report.hpp"
#include "codium/extension_security.hpp"

namespace codium {

namespace {

bool Has(const wxString& json, const wxString& key)
{
    return json.Find(wxString::Format(wxS("\"%s\""), key)) != wxNOT_FOUND;
}

} // namespace

CompatibilityReport CompatibilityReporter::Analyze(const wxString& manifestJson)
{
    CompatibilityReport report;
    ExtensionManifest manifest;
    wxString error;
    report.manifestValid = ExtensionSecurity::ValidateManifest(manifestJson, &manifest, &error);
    if (!report.manifestValid) {
        report.warnings.Add(error.empty() ? wxS("Invalid extension manifest.") : error);
        return report;
    }
    report.extensionId = manifest.publisher + wxS(".") + manifest.name;
    report.activationSupported = !Has(manifestJson, wxS("browser"));
    if (report.activationSupported) report.supportedFeatures.Add(wxS("Node.js main activation"));
    else report.unsupportedFeatures.Add(wxS("browser/web worker activation"));

    const wxArrayString supported = {
        wxS("commands"), wxS("configuration"), wxS("languages"), wxS("grammars"),
        wxS("themes"), wxS("debuggers"), wxS("taskDefinitions"), wxS("views")};
    for (const auto& feature : supported) {
        if (Has(manifestJson, feature)) report.supportedFeatures.Add(feature);
    }
    if (Has(manifestJson, wxS("enableProposedApi"))) {
        report.unsupportedFeatures.Add(wxS("proposed API"));
        report.warnings.Add(wxS("Proposed VS Code APIs require explicit compatibility work."));
    }
    if (Has(manifestJson, wxS("webview")) || Has(manifestJson, wxS("webviews"))) {
        report.unsupportedFeatures.Add(wxS("webviews"));
        report.warnings.Add(wxS("Webviews are not part of the native core startup path."));
    }
    if (Has(manifestJson, wxS("electron"))) {
        report.unsupportedFeatures.Add(wxS("Electron/private APIs"));
        report.warnings.Add(wxS("Electron internals are not available in Codium::Blocks."));
    }
    if (report.supportedFeatures.IsEmpty()) report.warnings.Add(wxS("No recognized contribution point was found."));
    return report;
}

} // namespace codium
