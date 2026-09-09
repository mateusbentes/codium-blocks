#include "codium/extension_security.hpp"
#include "codium/extension_registry.hpp"

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

#include <filesystem>
#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return 1;
    const wxString root = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + wxS("codium-blocks-security-smoke");
    std::filesystem::remove_all(root.ToStdString());
    wxFileName::Mkdir(root, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const wxString sample = root + wxFILE_SEP_PATH + wxS("sample.txt");
    {
        wxFile file(sample, wxFile::write);
        const wxString content = wxS("abc");
        file.Write(content.utf8_str().data(), content.utf8_str().length());
    }

    wxString digest;
    wxString error;
    if (!codium::ExtensionSecurity::ComputeSha256(sample, &digest, &error) ||
        digest != wxS("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
        std::cerr << "security-smoke: SHA-256 failed: " << error.ToStdString() << "\n";
        return 2;
    }

    codium::ExtensionManifest manifest;
    if (!codium::ExtensionSecurity::ValidateManifest(
            wxS("{\"name\":\"demo\",\"publisher\":\"test-publisher\",\"version\":\"1.2.3\",\"engines\":{\"vscode\":\"^1.80.0\"}}"),
            &manifest, &error) || manifest.name != wxS("demo") || manifest.publisher != wxS("test-publisher")) {
        std::cerr << "security-smoke: valid manifest rejected: " << error.ToStdString() << "\n";
        return 3;
    }
    if (codium::ExtensionSecurity::ValidateManifest(wxS("{\"name\":\"../escape\",\"publisher\":\"test\",\"version\":\"1\"}"), &manifest, &error)) {
        std::cerr << "security-smoke: unsafe manifest accepted\n";
        return 4;
    }
    if (!codium::ExtensionSecurity::IsAllowedRegistryUrl(wxS("https://open-vsx.org/api")) ||
        codium::ExtensionSecurity::IsAllowedRegistryUrl(wxS("file:///tmp/registry")) ||
        codium::ExtensionSecurity::IsAllowedRegistryUrl(wxS("https://user:pass@example.com"))) {
        std::cerr << "security-smoke: registry URL allowlist failed\n";
        return 5;
    }
    codium::ExtensionRegistry registry;
    if (!registry.AddRegistry(wxS("https://open-vsx.org/api"), &error) ||
        registry.AddRegistry(wxS("http://insecure.example"), &error) ||
        registry.AddRegistry(wxS("https://user:pass@example.com"), &error) ||
        !registry.VerifyArtifact(sample, digest, &error) ||
        registry.VerifyArtifact(sample, wxS("00"), &error)) {
        std::cerr << "security-smoke: registry or artifact verification failed: " << error.ToStdString() << "\n";
        return 6;
    }

    std::filesystem::remove_all(root.ToStdString());
    std::cout << "security-smoke: ok — SHA-256, manifest validation, and registry allowlist\n";
    return 0;
}
