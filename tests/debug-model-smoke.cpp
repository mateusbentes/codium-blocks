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

    codium::DapDebugSessionModel session;
    session.MarkStarted();
    if (session.State() != codium::DapRunState::Initializing ||
        codium::DapDebugSessionModel::StateName(session.State()) != wxS("initializing")) {
        std::cerr << "debug-model-smoke: DAP start state failed\n";
        return 10;
    }
    wxArrayInt requestedLines;
    requestedLines.Add(12);
    requestedLines.Add(24);
    session.SetRequestedBreakpoints(wxS("demo.cpp"), requestedLines);
    if (!session.UpdateBreakpointOptions(wxS("demo.cpp"), 12, wxS("counter > 0"), wxS("3"), wxS("counter=%d")) ||
        session.RequestedBreakpoints(wxS("demo.cpp"))[0].condition != wxS("counter > 0") ||
        session.RequestedBreakpoints(wxS("demo.cpp"))[0].hitCondition != wxS("3") ||
        session.RequestedBreakpoints(wxS("demo.cpp"))[0].logMessage != wxS("counter=%d")) {
        std::cerr << "debug-model-smoke: breakpoint options failed\n";
        return 16;
    }
    const wxString breakpointResponse =
        wxS("{\"type\":\"response\",\"command\":\"setBreakpoints\",\"success\":true,"
            "\"body\":{\"breakpoints\":[{\"id\":3,\"verified\":true,\"line\":12},"
            "{\"id\":4,\"verified\":false,\"line\":24,\"message\":\"pending source\"}]}}\n");
    wxString breakpointError;
    if (!session.ApplyBreakpointResponse(wxS("demo.cpp"), breakpointResponse, &breakpointError) ||
        session.Breakpoints(wxS("demo.cpp")).size() != 2 ||
        session.Breakpoints(wxS("demo.cpp"))[0].state != codium::DapBreakpointState::Verified ||
        session.Breakpoints(wxS("demo.cpp"))[0].id != 3 ||
        session.Breakpoints(wxS("demo.cpp"))[1].state != codium::DapBreakpointState::Rejected ||
        session.Breakpoints(wxS("demo.cpp"))[1].message != wxS("pending source")) {
        std::cerr << "debug-model-smoke: DAP breakpoint response failed\n";
        return 11;
    }
    if (session.ToggleRequestedBreakpoint(wxS("demo.cpp"), 24) ||
        session.RequestedBreakpointLines(wxS("demo.cpp")).size() != 1) {
        std::cerr << "debug-model-smoke: DAP breakpoint toggle failed\n";
        return 12;
    }
    codium::DapRefreshPlan stopped = session.ObserveMessage(
        wxS("{\"type\":\"event\",\"event\":\"stopped\",\"threadId\":7,"
            "\"reason\":\"breakpoint\"}"));
    if (session.State() != codium::DapRunState::Paused || !stopped.stateChanged ||
        !stopped.refreshThreads || stopped.threadId != 7 || stopped.reason != wxS("breakpoint")) {
        std::cerr << "debug-model-smoke: DAP stopped refresh plan failed\n";
        return 13;
    }
    const codium::DapRefreshPlan continued = session.ObserveMessage(
        wxS("{\"type\":\"event\",\"event\":\"continued\",\"threadId\":7}"));
    if (session.State() != codium::DapRunState::Running || !continued.clearTransientViews) {
        std::cerr << "debug-model-smoke: DAP continued refresh plan failed\n";
        return 14;
    }
    const codium::DapRefreshPlan terminated = session.ObserveMessage(
        wxS("{\"type\":\"event\",\"event\":\"terminated\"}"));
    if (session.State() != codium::DapRunState::Stopped || !terminated.clearTransientViews) {
        std::cerr << "debug-model-smoke: DAP terminated refresh plan failed\n";
        return 15;
    }

    std::vector<codium::DapBreakpoint> persistent = session.AllRequestedBreakpoints();
    if (!codium::DapBreakpointStore::Save(root, persistent, &error)) {
        std::cerr << "debug-model-smoke: breakpoint save failed: " << error.ToStdString() << "\n";
        return 17;
    }
    const auto restored = codium::DapBreakpointStore::Load(root);
    if (restored.size() != 1 || restored[0].sourcePath != wxS("demo.cpp") ||
        restored[0].condition != wxS("counter > 0") || restored[0].hitCondition != wxS("3") ||
        restored[0].logMessage != wxS("counter=%d")) {
        std::cerr << "debug-model-smoke: breakpoint persistence failed\n";
        return 18;
    }
    std::filesystem::remove_all(root.ToStdString());

    std::cout << "debug-model-smoke: ok — source mapping, persistent watches, Code::Blocks snapshots, and DAP session state\n";
    return 0;
}
