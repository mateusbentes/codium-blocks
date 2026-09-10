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

    codium::CodeBlocksDebugSnapshot frames;
    if (!codium::CodeBlocksDebugSnapshot::Parse(
            wxS("{\"dataKind\":\"frames\",\"activeFrame\":1,\"items\":["
                "{\"number\":0,\"function\":\"main\",\"file\":\"src/main.cpp\","
                "\"lineText\":\"12\",\"line\":12,\"valid\":true},"
                "{\"number\":1,\"function\":\"worker\",\"file\":\"src/worker.cpp\","
                "\"line\":24,\"valid\":false}]}"),
            &frames) || frames.dataKind != wxS("frames") || frames.activeFrame != 1 ||
        frames.frames.size() != 2 || frames.frames[0].function != wxS("main") ||
        !frames.frames[0].hasLine || frames.frames[0].line != 12 || frames.frames[1].valid) {
        std::cerr << "debug-model-smoke: frame snapshot parsing failed\n";
        return 5;
    }

    codium::CodeBlocksDebugSnapshot value;
    if (!codium::CodeBlocksDebugSnapshot::Parse(
            wxS("{\"dataKind\":\"variables\",\"expression\":\"counter\","
                "\"item\":{\"symbol\":\"counter\",\"value\":\"42\","
                "\"type\":\"int\",\"children\":[{\"symbol\":\"nested\","
                "\"value\":\"7\",\"type\":\"int\"}]}}"),
            &value) || value.dataKind != wxS("variables") || value.expression != wxS("counter") ||
        !value.hasValue || value.value.symbol != wxS("counter") || value.value.value != wxS("42") ||
        value.value.children.size() != 1 || value.value.children[0].symbol != wxS("nested")) {
        std::cerr << "debug-model-smoke: value snapshot parsing failed\n";
        return 6;
    }

    codium::CodeBlocksDebugSnapshot threads;
    if (!codium::CodeBlocksDebugSnapshot::Parse(
            wxS("{\"dataKind\":\"threads\",\"items\":[{\"active\":true,"
                "\"number\":2,\"info\":\"main thread\"}]}"),
            &threads) || threads.threads.size() != 1 || !threads.threads[0].active ||
        threads.threads[0].number != 2 || threads.threads[0].info != wxS("main thread")) {
        std::cerr << "debug-model-smoke: thread snapshot parsing failed\n";
        return 7;
    }

    codium::CodeBlocksDebugSnapshot breakpoints;
    if (!codium::CodeBlocksDebugSnapshot::Parse(
            wxS("{\"dataKind\":\"breakpoints\",\"items\":[{\"location\":"
                "\"src/main.cpp\",\"line\":12,\"enabled\":true,\"visible\":true,"
                "\"temporary\":false}]}"),
            &breakpoints) || breakpoints.breakpoints.size() != 1 ||
        breakpoints.breakpoints[0].location != wxS("src/main.cpp") ||
        breakpoints.breakpoints[0].line != 12 || !breakpoints.breakpoints[0].enabled) {
        std::cerr << "debug-model-smoke: breakpoint snapshot parsing failed\n";
        return 8;
    }

    codium::CodeBlocksDebugSnapshot invalid;
    wxString parseError;
    if (codium::CodeBlocksDebugSnapshot::Parse(wxS("{invalid"), &invalid, &parseError) || parseError.empty()) {
        std::cerr << "debug-model-smoke: invalid snapshot was accepted\n";
        return 9;
    }

    std::cout << "debug-model-smoke: ok — source mapping, persistent watches, and Code::Blocks snapshots\n";
    return 0;
}
