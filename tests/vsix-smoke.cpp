#include "codium/vsix_manager.hpp"

#include <wx/filename.h>
#include <wx/dir.h>
#include <wx/init.h>
#include <wx/stdpaths.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#include <cstdio>
#include <filesystem>
#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "vsix-smoke: wxWidgets initialization failed\n";
        return 1;
    }

    const wxString root = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH + wxS("codium-blocks-vsix-smoke");
    const wxString vsixPath = root + wxFILE_SEP_PATH + wxS("demo.vsix");
    const wxString installRoot = root + wxFILE_SEP_PATH + wxS("installed");
    std::filesystem::remove_all(root.ToStdString());
    wxFileName::Mkdir(root, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    {
        wxFFileOutputStream output(vsixPath);
        wxZipOutputStream archive(output);
        archive.PutNextEntry(wxS("extension/package.json"));
        const wxString manifest = wxS("{\"name\":\"demo\",\"publisher\":\"test\"}\n");
        archive.Write(manifest.utf8_str().data(), manifest.utf8_str().length());
        archive.PutNextEntry(wxS("extension/extension.js"));
        const wxString script = wxS("module.exports = {};\n");
        archive.Write(script.utf8_str().data(), script.utf8_str().length());
        archive.Close();
    }

    codium::VsixManager manager(installRoot);
    wxString message;
    if (!manager.Install(vsixPath, &message)) {
        std::cerr << "vsix-smoke: install failed: " << message.ToStdString() << "\n";
        return 1;
    }

    const wxString extracted = installRoot + wxFILE_SEP_PATH + wxS("demo") +
                               wxFILE_SEP_PATH + wxS("extension/package.json");
    if (!wxFileExists(extracted) || manager.ListInstalled().GetCount() != 1) {
        std::cerr << "vsix-smoke: extracted file or listing missing\n";
        return 1;
    }

    const wxString maliciousPath = root + wxFILE_SEP_PATH + wxS("malicious.vsix");
    {
        wxFFileOutputStream output(maliciousPath);
        wxZipOutputStream archive(output);
        archive.PutNextEntry(wxS("../escape.txt"));
        const wxString payload = wxS("must not be extracted\n");
        archive.Write(payload.utf8_str().data(), payload.utf8_str().length());
        archive.Close();
    }
    if (manager.Install(maliciousPath, &message) || wxFileExists(root + wxFILE_SEP_PATH + wxS("escape.txt"))) {
        std::cerr << "vsix-smoke: unsafe archive path was accepted\n";
        return 1;
    }

    std::remove(vsixPath.utf8_str().data());
    std::remove(maliciousPath.utf8_str().data());
    std::cout << "vsix-smoke: ok — cross-platform ZIP extraction\n";
    return 0;
}
