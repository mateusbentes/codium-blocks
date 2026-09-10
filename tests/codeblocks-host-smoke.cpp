#include "codium/codeblocks_host.hpp"

#include <iostream>

int main()
{
    if (codium::CodeBlocksHostContract::Version() != wxS("1.0") ||
        !codium::CodeBlocksHostContract::Supports(1, 0) ||
        codium::CodeBlocksHostContract::Supports(1, 1) ||
        codium::CodeBlocksHostContract::Supports(2, 0) ||
        codium::CodeBlocksHostContract::Supports(1, -1)) {
        std::cerr << "codeblocks-host-smoke: contract compatibility failed\n";
        return 1;
    }

    codium::CodeBlocksEventBus bus;
    bus.Publish(codium::CodeBlocksHostEvent{
        codium::CodeBlocksEventKind::ProjectOpened,
        wxS("/workspace/demo.cbp"), wxS("app"), wxEmptyString, wxEmptyString,
        wxS("Project opened"), wxEmptyString, wxEmptyString, wxEmptyString,
        wxEmptyString, 0, 0, 0, false, wxEmptyString, wxEmptyString});
    bus.Publish(codium::CodeBlocksHostEvent{
        codium::CodeBlocksEventKind::ProjectTarget,
        wxS("/workspace/demo.cbp"), wxS("Debug"), wxEmptyString, wxEmptyString,
        wxS("Project target enumerated"), wxEmptyString, wxS("gcc"),
        wxS("bin/demo"), wxS("bin"), 0, 0, 0, false, wxEmptyString, wxEmptyString});
    bus.Publish(codium::CodeBlocksHostEvent{
        codium::CodeBlocksEventKind::CompilerDiagnostic,
        wxS("/workspace/demo.cbp"), wxS("app"), wxS("Compiler"), wxEmptyString,
        wxS("missing header"), wxS("src/main.cpp"), wxEmptyString, wxEmptyString,
        wxEmptyString, 11, 4, 1, true, wxEmptyString, wxEmptyString});
    codium::CodeBlocksHostEvent renamed;
    renamed.kind = codium::CodeBlocksEventKind::ProjectFileRenamed;
    renamed.projectPath = wxS("/workspace/demo.cbp");
    renamed.filePath = wxS("src/new.cpp");
    renamed.oldFilePath = wxS("src/old.cpp");
    bus.Publish(renamed);
    if (bus.Empty()) {
        std::cerr << "codeblocks-host-smoke: event bus unexpectedly empty\n";
        return 2;
    }

    const auto events = bus.Drain();
    if (events.size() != 4 || !bus.Empty() ||
        CodeBlocksEventKindName(events[0].kind) != wxS("projectOpened") ||
        CodeBlocksEventKindName(events[1].kind) != wxS("projectTarget") ||
        events[1].compilerId != wxS("gcc") ||
        CodeBlocksEventKindName(events[2].kind) != wxS("compilerDiagnostic") ||
        events[2].line != 11 || events[2].column != 4 || !events[2].isError ||
        CodeBlocksEventKindName(events[3].kind) != wxS("projectFileRenamed") ||
        events[3].oldFilePath != wxS("src/old.cpp") || events[3].filePath != wxS("src/new.cpp")) {
        std::cerr << "codeblocks-host-smoke: event normalization failed\n";
        return 3;
    }

    std::cout << "codeblocks-host-smoke: ok — versioned contract and normalized event bus\n";
    return 0;
}
