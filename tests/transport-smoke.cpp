#include "codium/dap_client.hpp"
#include "codium/terminal_session.hpp"

#include <wx/init.h>
#include <wx/string.h>

#include <iostream>

namespace {

bool WaitForTerminal(codium::TerminalSession& terminal, const wxString& expected)
{
    for (int i = 0; i < 150 && terminal.IsRunning(); ++i) {
        wxMilliSleep(10);
        for (const auto& line : terminal.Poll()) {
            if (line.Find(expected) != wxNOT_FOUND) return true;
        }
    }
    return false;
}

bool WaitForRawTerminal(codium::TerminalSession& terminal, const wxString& expected)
{
    for (int i = 0; i < 150 && terminal.IsRunning(); ++i) {
        wxMilliSleep(10);
        if (terminal.PollRaw().Find(expected) != wxNOT_FOUND) return true;
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

    codium::TerminalSession terminal(nullptr, wxID_HIGHEST + 700);
    wxArrayString terminalArguments;
    terminalArguments.Add(fakeTerminal);
    wxString error;
    if (!terminal.Start(wxS("node"), terminalArguments, root, &error) ||
        (terminal.BackendName() == wxS("ConPTY") && !terminal.Resize(100, 30)) ||
        !terminal.Write(wxS("ping\n")) ||
        !WaitForTerminal(terminal, wxS("pong"))) {
        std::cerr << "transport-smoke: terminal failed (backend=" << terminal.BackendName().ToStdString()
                  << "): " << error.ToStdString() << "\n";
        return 1;
    }
    if (!terminal.Write(wxS("ansi\n")) || !WaitForRawTerminal(terminal, wxString::FromUTF8("\x1b[31m"))) {
        std::cerr << "transport-smoke: ANSI output or PTY resize failed\n";
        return 1;
    }
    terminal.Write(wxS("exit\n"));
    terminal.Stop();

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
        !WaitForDap(dap, wxS("\"allThreadsContinued\":true"))) {
        std::cerr << "transport-smoke: DAP failed: " << error.ToStdString() << "\n";
        return 1;
    }
    wxArrayInt breakpoints;
    breakpoints.Add(12);
    if (!dap.SetBreakpoints(wxS("demo.cpp"), breakpoints) || !WaitForDap(dap, wxS("\"verified\":true")) ||
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
