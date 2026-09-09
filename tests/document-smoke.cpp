#include "codium/document.hpp"

#include <wx/init.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <cstdio>
#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "document-smoke: wxWidgets initialization failed\n";
        return 1;
    }

    const wxString path = wxStandardPaths::Get().GetTempDir() + wxFILE_SEP_PATH +
                          wxS("codium-blocks-document-smoke.txt");
    const wxString expected = wxS("# Codium::Blocks\nUTF-8: café\n");

    codium::Document document(path);
    document.SetText(expected);
    wxString error;
    if (!document.Save(&error)) {
        std::cerr << "document-smoke: save failed\n";
        return 1;
    }

    codium::Document loaded;
    if (!loaded.Load(path, &error) || loaded.Text() != expected || loaded.IsDirty()) {
        std::cerr << "document-smoke: load/round-trip failed\n";
        return 1;
    }

    std::remove(path.utf8_str().data());
    std::cout << "document-smoke: ok — UTF-8 open/save and clean state\n";
    return 0;
}
