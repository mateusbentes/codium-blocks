#include "codium/debug_model.hpp"

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/utils.h>

#include <filesystem>
#include <iostream>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) return 1;
    codium::SourceMapper mapper;
    mapper.Add(wxS("/remote"), wxS("/local"));
    mapper.Add(wxS("/remote/project"), wxS("/workspace/project"));
    if (mapper.Map(wxS("/remote/project/main.cpp")) != wxS("/workspace/project/main.cpp") ||
        mapper.Map(wxS("/remote/lib.cpp")) != wxS("/local/lib.cpp") ||
        mapper.Map(wxS("/other.cpp")) != wxS("/other.cpp") ||
        mapper.ToJson().Find(wxS("/remote/project")) == wxNOT_FOUND) {
        std::cerr << "debug-model-smoke: source mapping failed\n";
        return 2;
    }

    const wxString root = wxFileName::GetTempDir() + wxFILE_SEP_PATH + wxS("codium-blocks-debug-model-smoke");
    const wxString data = root + wxFILE_SEP_PATH + wxS("data");
    std::filesystem::remove_all(root.ToStdString());
    wxSetEnv(wxS("CODIUM_BLOCKS_DATA"), data);
    wxArrayString expressions;
    expressions.Add(wxS("counter"));
    expressions.Add(wxS("items.size()"));
    wxString error;
    if (!codium::WatchStore::Save(root, expressions, &error)) {
        std::cerr << "debug-model-smoke: watch save failed: " << error.ToStdString() << "\n";
        return 3;
    }
    const wxArrayString loaded = codium::WatchStore::Load(root);
    if (loaded != expressions) {
        std::cerr << "debug-model-smoke: watch persistence failed\n";
        return 4;
    }
    std::filesystem::remove_all(root.ToStdString());
    std::cout << "debug-model-smoke: ok — source mapping and persistent watches\n";
    return 0;
}
