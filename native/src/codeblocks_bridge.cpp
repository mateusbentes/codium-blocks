// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/codeblocks_bridge.hpp"

#include <wx/dir.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/xml/xml.h>

#include <algorithm>

namespace codium {
namespace {

wxString JoinPath(const wxString& parent, const wxString& child)
{
    if (parent.empty()) return child;
    wxString result = parent;
    if (!result.EndsWith(wxS("/")) && !result.EndsWith(wxS("\\"))) result += wxFILE_SEP_PATH;
    result += child;
    return result;
}

wxString ChildAttribute(wxXmlNode* parent, const wxString& childName, const wxString& attribute)
{
    if (!parent) return wxEmptyString;
    for (wxXmlNode* child = parent->GetChildren(); child; child = child->GetNext()) {
        if (child->GetName() != childName) continue;
        for (wxXmlAttribute* property = child->GetAttributes(); property; property = property->GetNext()) {
            if (property->GetName() == attribute) return property->GetValue();
        }
    }
    return wxEmptyString;
}

wxString AttributeValue(wxXmlNode* node, const wxString& attribute, const wxString& fallback)
{
    if (!node) return fallback;
    for (wxXmlAttribute* property = node->GetAttributes(); property; property = property->GetNext()) {
        if (property->GetName() == attribute) return property->GetValue();
    }
    return fallback;
}

wxXmlNode* ChildNode(wxXmlNode* parent, const wxString& childName)
{
    if (!parent) return nullptr;
    for (wxXmlNode* child = parent->GetChildren(); child; child = child->GetNext()) {
        if (child->GetName() == childName) return child;
    }
    return nullptr;
}

bool IsNativeLibrary(const wxString& filename)
{
    const wxString lower = filename.Lower();
#if defined(__WXMSW__)
    return lower.EndsWith(wxS(".dll"));
#elif defined(__WXMAC__)
    return lower.EndsWith(wxS(".dylib")) || lower.Contains(wxS(".bundle"));
#else
    return lower.EndsWith(wxS(".so")) || lower.Contains(wxS(".so."));
#endif
}

wxString PluginStem(const wxString& filename)
{
    wxFileName path(filename);
    wxString name = path.GetName();
#if !defined(__WXMSW__)
    if (name.StartsWith(wxS("lib"))) name = name.Mid(3);
#endif
    return name;
}

} // namespace

CodeBlocksBridge::CodeBlocksBridge(const wxString& root)
    : requestedRoot_(root)
{
}

wxArrayString CodeBlocksBridge::DefaultRoots()
{
    wxArrayString roots;
    auto add = [&roots](const wxString& value) {
        if (value.empty()) return;
        wxFileName normalized(value);
        normalized.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_TILDE);
        const wxString candidate = normalized.GetFullPath();
        if (roots.Index(candidate) == wxNOT_FOUND) roots.Add(candidate);
    };

    wxString value;
    if (wxGetEnv(wxS("CODEBLOCKS_ROOT"), &value)) add(value);
    if (wxGetEnv(wxS("CODEBLOCKS_HOME"), &value)) add(value);
#if defined(__WXMSW__)
    if (wxGetEnv(wxS("ProgramFiles"), &value)) add(JoinPath(value, wxS("CodeBlocks")));
    if (wxGetEnv(wxS("ProgramFiles(x86)"), &value)) add(JoinPath(value, wxS("CodeBlocks")));
#elif defined(__WXMAC__)
    add(wxS("/Applications/CodeBlocks.app/Contents/Resources"));
    add(wxS("/Applications/CodeBlocks.app/Contents"));
    add(wxS("/opt/local"));
    add(wxS("/opt/homebrew"));
#else
    add(wxS("/usr"));
    add(wxS("/usr/local"));
    add(wxS("/opt/codeblocks"));
    add(wxS("/opt"));
    add(wxS("~/.local"));
#endif
    // GetInstallPrefix was removed from wxWidgets 3.3. The platform-specific
    // roots above cover packaged installations; also inspect the executable's
    // directory without relying on a version-specific wx API.
    const wxString executableDirectory = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
    add(executableDirectory);
    return roots;
}

bool CodeBlocksBridge::ParseManifest(const wxString& path, CodeBlocksPluginInfo* plugin, wxString* error) const
{
    if (!plugin) return false;
    wxXmlDocument document;
    if (!document.Load(path)) {
        if (error) *error = wxString::Format(wxS("Could not parse Code::Blocks manifest: %s"), path);
        return false;
    }
    wxXmlNode* root = document.GetRoot();
    if (!root || root->GetName() != wxS("CodeBlocks_plugin_manifest_file")) {
        if (error) *error = wxString::Format(wxS("Invalid Code::Blocks manifest root: %s"), path);
        return false;
    }

    wxXmlNode* sdk = ChildNode(root, wxS("SdkVersion"));
    if (!sdk) {
        if (error) *error = wxString::Format(wxS("Manifest has no SdkVersion: %s"), path);
        return false;
    }
    plugin->sdkMajor = wxAtoi(AttributeValue(sdk, wxS("major"), wxS("0")));
    plugin->sdkMinor = wxAtoi(AttributeValue(sdk, wxS("minor"), wxS("0")));
    plugin->sdkRelease = wxAtoi(AttributeValue(sdk, wxS("release"), wxS("0")));

    wxXmlNode* declaration = ChildNode(root, wxS("Plugin"));
    if (!declaration) {
        if (error) *error = wxString::Format(wxS("Manifest has no Plugin declaration: %s"), path);
        return false;
    }
    plugin->name = AttributeValue(declaration, wxS("name"), wxEmptyString);
    plugin->title = ChildAttribute(declaration, wxS("Value"), wxS("title"));
    plugin->version = ChildAttribute(declaration, wxS("Value"), wxS("version"));
    plugin->description = ChildAttribute(declaration, wxS("Value"), wxS("description"));
    plugin->license = ChildAttribute(declaration, wxS("Value"), wxS("license"));
    if (plugin->title.empty()) plugin->title = plugin->name;
    plugin->manifestPath = path;
    plugin->manifestValid = true;
    plugin->loadable = false;
    plugin->status = wxString::Format(wxS("manifest valid; SDK %d.%d.%d; native loading disabled"),
                                      plugin->sdkMajor, plugin->sdkMinor, plugin->sdkRelease);
    return true;
}

bool CodeBlocksBridge::ScanPluginDirectory(const wxString& directory, wxString* error)
{
    if (!wxDirExists(directory)) return false;
    wxDir dir(directory);
    if (!dir.IsOpened()) {
        if (error) *error = wxString::Format(wxS("Could not open Code::Blocks plugin directory: %s"), directory);
        return false;
    }

    wxString filename;
    bool found = dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES);
    while (found) {
        if (!IsNativeLibrary(filename)) {
            found = dir.GetNext(&filename);
            continue;
        }
        CodeBlocksPluginInfo plugin;
        plugin.filePath = JoinPath(directory, filename);
        plugin.name = PluginStem(filename);
        plugin.title = plugin.name;
        plugin.nativeLibrary = true;
        plugin.status = wxS("manifest not found; native loading disabled");

        const wxString stem = PluginStem(filename);
        const wxString candidates[] = {
            JoinPath(directory, wxS("manifest.xml")),
            JoinPath(directory, wxS("resources/manifest.xml")),
            JoinPath(directory, stem + wxS(".manifest.xml"))
        };
        for (const auto& manifest : candidates) {
            if (!wxFileExists(manifest)) continue;
            wxString parseError;
            if (!ParseManifest(manifest, &plugin, &parseError) && error) *error = parseError;
            break;
        }

        const auto duplicate = std::find_if(plugins_.begin(), plugins_.end(), [&plugin](const CodeBlocksPluginInfo& item) {
            return item.filePath == plugin.filePath;
        });
        if (duplicate == plugins_.end()) plugins_.push_back(plugin);
        found = dir.GetNext(&filename);
    }
    return true;
}

bool CodeBlocksBridge::Discover(wxString* error)
{
    available_ = false;
    canLoadPlugins_ = false;
    root_.clear();
    sdkIncludeDirectory_.clear();
    pluginDirectories_.clear();
    plugins_.clear();

    wxArrayString roots;
    if (!requestedRoot_.empty()) roots.Add(requestedRoot_);
    else roots = DefaultRoots();

    for (const auto& candidate : roots) {
        if (!wxDirExists(candidate)) continue;
        const wxString includeCandidates[] = {
            JoinPath(candidate, wxS("include")),
            JoinPath(candidate, wxS("include/codeblocks")),
            JoinPath(candidate, wxS("src/include")),
            JoinPath(candidate, wxS("src/include/codeblocks")),
            JoinPath(candidate, wxS("Contents/Resources/include")),
            JoinPath(candidate, wxS("Contents/Resources/include/codeblocks"))
        };
        const wxString pluginCandidates[] = {
            JoinPath(candidate, wxS("lib/codeblocks/plugins")),
            JoinPath(candidate, wxS("lib/codeblocks")),
            JoinPath(candidate, wxS("share/codeblocks/plugins")),
            JoinPath(candidate, wxS("share/codeblocks")),
            JoinPath(candidate, wxS("plugins")),
            JoinPath(candidate, wxS("Contents/PlugIns")),
            JoinPath(candidate, wxS("Contents/Resources/share/codeblocks/plugins"))
        };

        wxString includeDirectory;
        for (const auto& include : includeCandidates) {
            if (wxFileExists(JoinPath(include, wxS("cbplugin.h")))) {
                includeDirectory = include;
                break;
            }
        }
        wxArrayString foundPluginDirectories;
        for (const auto& directory : pluginCandidates) {
            if (wxDirExists(directory)) foundPluginDirectories.Add(directory);
        }
        if (includeDirectory.empty() && foundPluginDirectories.empty()) continue;

        root_ = candidate;
        sdkIncludeDirectory_ = includeDirectory;
        pluginDirectories_ = foundPluginDirectories;
        available_ = true;
        for (const auto& directory : pluginDirectories_) ScanPluginDirectory(directory, error);
        break;
    }

    if (!available_ && error) {
        *error = requestedRoot_.empty()
            ? wxS("Code::Blocks SDK or plugin directories were not found")
            : wxString::Format(wxS("Code::Blocks installation not found under: %s"), requestedRoot_);
    }
    return available_;
}

wxString CodeBlocksBridge::LoadPolicy() const
{
    return wxS("Preflight only: Codium::Blocks does not load Code::Blocks shared libraries until a matching SDK host ABI is attached. Native plugins depend on Manager, PluginManager, cbPlugin, and event infrastructure.");
}

} // namespace codium
