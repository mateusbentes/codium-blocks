// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include <wx/arrstr.h>
#include <wx/string.h>

#include <vector>

namespace codium {

struct CodeBlocksPluginInfo final {
    wxString filePath;
    wxString manifestPath;
    wxString name;
    wxString title;
    wxString version;
    wxString description;
    wxString license;
    int sdkMajor = 0;
    int sdkMinor = 0;
    int sdkRelease = 0;
    bool nativeLibrary = false;
    bool manifestValid = false;
    bool loadable = false;
    wxString status;
};

class CodeBlocksBridge final {
public:
    explicit CodeBlocksBridge(const wxString& root = wxEmptyString);

    bool Discover(wxString* error = nullptr);
    bool IsAvailable() const { return available_; }
    bool CanLoadPlugins() const { return canLoadPlugins_; }
    const wxString& Root() const { return root_; }
    const wxString& SdkIncludeDirectory() const { return sdkIncludeDirectory_; }
    const wxArrayString& PluginDirectories() const { return pluginDirectories_; }
    const std::vector<CodeBlocksPluginInfo>& Plugins() const { return plugins_; }

    wxString LoadPolicy() const;
    static wxArrayString DefaultRoots();

private:
    bool ScanPluginDirectory(const wxString& directory, wxString* error);
    bool ParseManifest(const wxString& path, CodeBlocksPluginInfo* plugin, wxString* error) const;

    wxString requestedRoot_;
    wxString root_;
    wxString sdkIncludeDirectory_;
    wxArrayString pluginDirectories_;
    std::vector<CodeBlocksPluginInfo> plugins_;
    bool available_ = false;
    bool canLoadPlugins_ = false;
};

} // namespace codium
