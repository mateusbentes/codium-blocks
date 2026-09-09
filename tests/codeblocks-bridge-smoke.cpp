#include "codium/codeblocks_bridge.hpp"

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "codeblocks-bridge-smoke: wxWidgets initialization failed\n";
        return 1;
    }

    const wxString root = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + wxS("codium-blocks-codeblocks-bridge");
    const std::filesystem::path rootPath(root.ToStdString());
    std::filesystem::remove_all(rootPath);
    std::filesystem::create_directories(rootPath / "include");
    std::filesystem::create_directories(rootPath / "plugins" / "resources");
    std::ofstream(rootPath / "include" / "cbplugin.h") << "// fake SDK header\n";
    std::ofstream(rootPath / "plugins" / "libdemo.so") << "not a real shared library\n";
    std::ofstream(rootPath / "plugins" / "resources" / "manifest.xml")
        << R"xml(<?xml version="1.0" encoding="UTF-8"?>
<CodeBlocks_plugin_manifest_file>
  <SdkVersion major="1" minor="36" release="0" />
  <Plugin name="DemoPlugin">
    <Value title="Demo Plugin" />
    <Value version="0.1" />
    <Value description="A manifest used by the bridge smoke test." />
    <Value license="GPL" />
  </Plugin>
</CodeBlocks_plugin_manifest_file>
)xml";

    codium::CodeBlocksBridge bridge(root);
    wxString error;
    if (!bridge.Discover(&error) || !bridge.IsAvailable() || bridge.SdkIncludeDirectory().empty() ||
        bridge.PluginDirectories().GetCount() != 1 || bridge.Plugins().size() != 1) {
        std::cerr << "codeblocks-bridge-smoke: discovery failed: " << error.ToStdString() << "\n";
        return 2;
    }
    const auto& plugin = bridge.Plugins().front();
    if (plugin.name != wxS("DemoPlugin") || plugin.title != wxS("Demo Plugin") || plugin.version != wxS("0.1") ||
        plugin.sdkMajor != 1 || plugin.sdkMinor != 36 || plugin.sdkRelease != 0 || !plugin.manifestValid ||
        !plugin.nativeLibrary || plugin.loadable || bridge.CanLoadPlugins()) {
        std::cerr << "codeblocks-bridge-smoke: manifest or load policy failed\n";
        return 3;
    }
    if (bridge.LoadPolicy().Find(wxS("Preflight only")) == wxNOT_FOUND) {
        std::cerr << "codeblocks-bridge-smoke: policy text missing\n";
        return 4;
    }

    std::filesystem::remove_all(rootPath);
    std::cout << "codeblocks-bridge-smoke: ok — SDK discovery and safe plugin preflight\n";
    return 0;
}
