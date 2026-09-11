// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/extension_security.hpp"
#include "codium/extension_registry.hpp"
#include "codium/signature_verifier.hpp"

#include <wx/filename.h>
#include <wx/file.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

#include <filesystem>
#include <iostream>

#if defined(CODIUM_BLOCKS_HAVE_OPENSSL)
#define CODIUM_BLOCKS_SIGNATURE_TEST_ENABLED 1
#endif

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
    const wxString empty = root + wxFILE_SEP_PATH + wxS("empty.bin");
    { wxFile file(empty, wxFile::write); }

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
    if (registry.OpenVsxSearchUrl(wxS("https://open-vsx.org"), wxS("C++ tools")) !=
            wxS("https://open-vsx.org/api/-/search?query=C%2B%2B+tools") ||
        !registry.CacheCatalog(wxS("https://open-vsx.org"), wxS("C++ tools"), wxS("{\"extensions\":[]}"), &error)) {
        std::cerr << "security-smoke: Open VSX URL/cache write failed: " << error.ToStdString() << "\n";
        return 7;
    }
    wxString cached;
    if (!registry.LoadCachedCatalog(wxS("https://open-vsx.org"), wxS("C++ tools"), &cached) ||
        cached != wxS("{\"extensions\":[]}")) {
        std::cerr << "security-smoke: Open VSX cache read failed\n";
        return 8;
    }
    if (codium::SignatureVerifier::VerifyEd25519File(
            sample,
            wxS("0000000000000000000000000000000000000000000000000000000000000000"),
            wxS("0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"),
            &error)) {
        std::cerr << "security-smoke: invalid Ed25519 signature was accepted\n";
        return 9;
    }
#if defined(CODIUM_BLOCKS_SIGNATURE_TEST_ENABLED)
    if (!codium::SignatureVerifier::VerifyEd25519File(
            empty,
            wxS("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"),
            wxS("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
            &error)) {
        std::cerr << "security-smoke: RFC 8032 Ed25519 vector failed: " << error.ToStdString() << "\n";
        return 10;
    }
#endif

    std::filesystem::remove_all(root.ToStdString());
    std::cout << "security-smoke: ok — SHA-256, manifest validation, and registry allowlist\n";
    return 0;
}
