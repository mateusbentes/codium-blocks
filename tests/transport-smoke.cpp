#include "codium/dap_client.hpp"
#include "codium/terminal_session.hpp"

#include <wx/init.h>
#include <wx/string.h>

#include <iostream>
#include <vector>

namespace {

bool WaitForTerminal(codium::TerminalSession& terminal, const wxString& expected)
{
    for (int i = 0; i < 150; ++i) {
        wxMilliSleep(10);
        for (const auto& line : terminal.Poll()) {
            if (line.Find(expected) != wxNOT_FOUND) return true;
        }
        if (!terminal.IsRunning() && i > 10) break;
    }
    return false;
}

bool WaitForRawTerminal(codium::TerminalSession& terminal, const wxString& expected)
{
    for (int i = 0; i < 150; ++i) {
        wxMilliSleep(10);
        if (terminal.PollRaw().Find(expected) != wxNOT_FOUND) return true;
        if (!terminal.IsRunning() && i > 10) break;
    }
    return false;
}

bool WaitForDap(codium::DapClient& dap, const wxString& expected)
{
    for (int i = 0; i < 150 && dap.IsRunning(); ++i) {
        wxMilliSleep(10);
        for (const auto& message : dap.Poll()) {
            if (message.Find(expected) != wxNOT_FOUND) return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    wxInitializer initializer;
    if (!initializer.IsOk() || argc < 2) {
        std::cerr << "transport-smoke: initialization or test root failed\n";
        return 1;
    }

    const wxString root = wxString::FromUTF8(argv[1]);
    const wxString fakeTerminal = root + wxS("/tests/fake-terminal.mjs");
    const wxString fakeDap = root + wxS("/tests/fake-dap.mjs");
    const wxString conPtyClient = argc >= 3 ? wxString::FromUTF8(argv[2]) : wxString(wxEmptyString);

    codium::TerminalSession terminal(nullptr, wxID_HIGHEST + 700);
    wxString requireConPty;
    const bool requireConPtyBackend = wxGetEnv(wxS("CODIUM_BLOCKS_REQUIRE_CONPTY"), &requireConPty) &&
        !requireConPty.empty() && requireConPty != wxS("0");
    wxString terminalProgram = wxS("node");
    wxArrayString terminalArguments;
    bool useNativeConPtyClient = false;
#if defined(__WXMSW__)
    if (requireConPtyBackend) {
        // Validate ConPTY with a native console program. This keeps the smoke
        // independent of cmd.exe's line editor and Node's Windows stdio path.
        if (conPtyClient.empty()) {
            std::cerr << "transport-smoke: native ConPTY client was not configured\n";
            return 1;
        }
        terminalProgram = conPtyClient;
        useNativeConPtyClient = true;
    } else {
        terminalArguments.Add(fakeTerminal);
    }
#else
    terminalArguments.Add(fakeTerminal);
#endif
    // The fake Node process validates byte transport on the portable path. The
    // required Windows path uses cmd.exe so ConPTY is tested with a native
    // interactive console process rather than Node's separate stdio layer.
#if defined(__WXMSW__)
    if (requireConPtyBackend) wxUnsetEnv(wxS("CODIUM_BLOCKS_DISABLE_CONPTY"));
    else wxSetEnv(wxS("CODIUM_BLOCKS_DISABLE_CONPTY"), wxS("1"));
#else
    (void)requireConPtyBackend;
#endif
    wxString error;
    if (!terminal.Start(terminalProgram, terminalArguments, root, &error)) {
        std::cerr << "transport-smoke: terminal start failed (backend=" << terminal.BackendName().ToStdString()
                  << "): " << error.ToStdString() << "\n";
        return 1;
    }
#if defined(__WXMSW__)
    if (requireConPtyBackend && terminal.BackendName() != wxS("ConPTY")) {
        std::cerr << "transport-smoke: ConPTY was required but backend was "
                  << terminal.BackendName().ToStdString() << "\n";
        return 1;
    }
#endif
    if (!WaitForTerminal(terminal, wxS("ready"))) {
        std::cerr << "transport-smoke: terminal process did not become ready (backend="
                  << terminal.BackendName().ToStdString() << ", running="
                  << (terminal.IsRunning() ? "true" : "false") << ")\n";
        return 1;
    }
    if (terminal.BackendName() == wxS("ConPTY") && !terminal.Resize(100, 30)) {
        std::cerr << "transport-smoke: ConPTY resize unavailable; continuing with the negotiated size\n";
    }
    // ConPTY translates input into console key events. A console Enter is a
    // carriage return; do not append LF, which becomes a second input event.
    terminal.PollRaw();
    const wxString pingCommand = useNativeConPtyClient ? wxS("ping\r") : wxS("ping\r\n");
    if (!terminal.Write(pingCommand)) {
        std::cerr << "transport-smoke: terminal write failed (backend=" << terminal.BackendName().ToStdString()
                  << ")\n";
        return 1;
    }
    if (!WaitForTerminal(terminal, wxS("pong"))) {
        std::cerr << "transport-smoke: terminal output timeout (backend=" << terminal.BackendName().ToStdString()
                  << ", running=" << (terminal.IsRunning() ? "true" : "false") << ")\n";
        return 1;
    }
    const wxString ansiCommand = useNativeConPtyClient ? wxS("ansi\r") : wxS("ansi\r\n");
    if (!terminal.Write(ansiCommand) || !WaitForRawTerminal(terminal, wxString::FromUTF8("\x1b[31m"))) {
        std::cerr << "transport-smoke: ANSI output or PTY resize failed\n";
        return 1;
    }
    terminal.Write(useNativeConPtyClient ? wxS("exit\r") : wxS("exit\r\n"));
    terminal.Stop();
#if !defined(__WXMSW__)
    codium::TerminalSession stubborn(nullptr, wxID_HIGHEST + 702);
    wxArrayString stubbornArguments;
    stubbornArguments.Add(wxS("-c"));
    stubbornArguments.Add(wxS("trap '' HUP TERM; while :; do sleep 1; done"));
    if (!stubborn.Start(wxS("/bin/sh"), stubbornArguments, root, &error)) {
        std::cerr << "transport-smoke: stubborn shell start failed: " << error.ToStdString() << "\n";
        return 1;
    }
    stubborn.Stop();
    if (stubborn.IsRunning()) {
        std::cerr << "transport-smoke: stubborn shell remained running after Stop\n";
        return 1;
    }
#endif
    wxUnsetEnv(wxS("CODIUM_BLOCKS_DISABLE_CONPTY"));

    codium::DapClient dap(nullptr, wxID_HIGHEST + 701);
    wxArrayString dapArguments;
    dapArguments.Add(fakeDap);
    if (!dap.Start(wxS("node"), dapArguments, root, &error) ||
        !dap.SendRequest(wxS("initialize"), wxS("{\"clientID\":\"codium-blocks\"}")) ||
        !WaitForDap(dap, wxS("\"event\":\"initialized\"")) ||
        !dap.ConfigurationDone() || !WaitForDap(dap, wxS("\"command\":\"configurationDone\"")) ||
        !dap.SendRequest(wxS("launch"), wxS("{\"program\":\"demo\"}")) ||
        !WaitForDap(dap, wxS("\"command\":\"launch\"")) ||
        !dap.SendRequest(wxS("threads")) ||
        !WaitForDap(dap, wxS("\"name\":\"main\"")) ||
        !dap.SendRequest(wxS("continue"), wxS("{\"threadId\":1}")) ||
        !WaitForDap(dap, wxS("\"event\":\"continued\"")) ||
        !dap.SendRequest(wxS("pause"), wxS("{\"threadId\":1}")) ||
        !WaitForDap(dap, wxS("\"event\":\"stopped\""))) {
        std::cerr << "transport-smoke: DAP failed: " << error.ToStdString() << "\n";
        return 1;
    }
    std::vector<codium::DapBreakpointRequest> breakpoints;
    breakpoints.push_back(codium::DapBreakpointRequest{12, wxS("counter > 0"), wxS("3"), wxS("counter=%d")});
    std::vector<codium::DapFunctionBreakpointRequest> functionBreakpoints;
    codium::DapFunctionBreakpointRequest functionBreakpoint;
    functionBreakpoint.name = wxS("main");
    functionBreakpoint.condition = wxS("counter > 0");
    functionBreakpoint.hitCondition = wxS("3");
    functionBreakpoints.push_back(functionBreakpoint);
    std::vector<codium::DapDataBreakpointRequest> dataBreakpoints;
    codium::DapDataBreakpointRequest dataBreakpoint;
    dataBreakpoint.dataId = wxS("counter");
    dataBreakpoint.accessType = wxS("write");
    dataBreakpoints.push_back(dataBreakpoint);
    if (!dap.SetBreakpoints(wxS("demo.cpp"), breakpoints) ||
        !WaitForDap(dap, wxS("\"optionsAccepted\":true,\"breakpoints\"")) ||
        !dap.SetFunctionBreakpoints(functionBreakpoints) ||
        !WaitForDap(dap, wxS("\"optionsAccepted\":true,\"breakpoints\"")) ||
        !dap.SetDataBreakpoints(dataBreakpoints) ||
        !WaitForDap(dap, wxS("\"optionsAccepted\":true,\"breakpoints\"")) ||
        !dap.RequestStackTrace(1) || !WaitForDap(dap, wxS("\"stackFrames\"")) ||
        !dap.RequestScopes(7) || !WaitForDap(dap, wxS("\"variablesReference\":42")) ||
        !dap.RequestVariables(42) || !WaitForDap(dap, wxS("\"answer\"")) ||
        !dap.Evaluate(wxS("answer"), 7) || !WaitForDap(dap, wxS("\"result\":\"42\""))) {
        std::cerr << "transport-smoke: DAP 0.9 requests failed\n";
        return 1;
    }
    dap.Stop();

    std::cout << "transport-smoke: ok — interactive terminal and DAP Content-Length transport\n";
    return 0;
}
