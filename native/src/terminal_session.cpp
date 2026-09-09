#include "codium/terminal_session.hpp"

#include <wx/utils.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#if defined(__WXMSW__)
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#endif

namespace codium {

namespace {

wxArrayString RawToLines(const std::string& raw, std::string& pending)
{
    wxArrayString lines;
    pending.append(raw);
    size_t newline = std::string::npos;
    while ((newline = pending.find('\n')) != std::string::npos) {
        std::string line = pending.substr(0, newline);
        pending.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.Add(wxString::FromUTF8(line.data(), line.size()));
    }
    return lines;
}

#if defined(__WXMSW__)

using HPCON = void*;
using CreatePseudoConsoleFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
using ResizePseudoConsoleFn = HRESULT(WINAPI*)(HPCON, COORD);
using ClosePseudoConsoleFn = void(WINAPI*)(HPCON);

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

#endif

} // namespace

TerminalSession::TerminalSession(wxEvtHandler* owner, int processId)
    : owner_(owner), processId_(processId)
{
}

TerminalSession::~TerminalSession()
{
    Stop();
}

#if defined(__WXMSW__)

bool StartConPty(TerminalSession* session, const wxString& program, const wxArrayString& arguments,
                 const wxString& workingDirectory, wxString* error)
{
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    auto create = reinterpret_cast<CreatePseudoConsoleFn>(GetProcAddress(kernel, "CreatePseudoConsole"));
    if (!create) return false;

    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite, &security, 0) ||
        !CreatePipe(&outputRead, &outputWrite, &security, 0)) {
        if (inputRead) CloseHandle(inputRead);
        if (inputWrite) CloseHandle(inputWrite);
        if (outputRead) CloseHandle(outputRead);
        if (outputWrite) CloseHandle(outputWrite);
        return false;
    }
    SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

    COORD size{120, 32};
    HPCON console = nullptr;
    if (FAILED(create(size, inputRead, outputWrite, 0, &console))) {
        CloseHandle(inputRead); CloseHandle(inputWrite); CloseHandle(outputRead); CloseHandle(outputWrite);
        return false;
    }
    CloseHandle(inputRead);
    CloseHandle(outputWrite);

    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attributeSize));
    if (!attributes || !InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console,
                                   sizeof(console), nullptr, nullptr)) {
        if (attributes) HeapFree(GetProcessHeap(), 0, attributes);
        auto close = reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(kernel, "ClosePseudoConsole"));
        if (close) close(console);
        CloseHandle(inputWrite); CloseHandle(outputRead);
        return false;
    }

    wxString command = program;
    for (const auto& argument : arguments) {
        command += wxS(" \"") + argument + wxS("\"");
    }
    std::wstring commandLine = command.ToStdWstring();
    commandLine.push_back(L'\0');
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION processInfo{};
    std::wstring cwd = workingDirectory.ToStdWstring();
    const BOOL started = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                                        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                                        nullptr, cwd.empty() ? nullptr : cwd.c_str(), &startup.StartupInfo, &processInfo);
    DeleteProcThreadAttributeList(attributes);
    HeapFree(GetProcessHeap(), 0, attributes);
    if (!started) {
        auto close = reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(kernel, "ClosePseudoConsole"));
        if (close) close(console);
        CloseHandle(inputWrite); CloseHandle(outputRead);
        if (error) *error = wxS("CreateProcessW failed for ConPTY.");
        return false;
    }
    CloseHandle(processInfo.hThread);
    session->pseudoConsole_ = console;
    session->childProcess_ = processInfo.hProcess;
    session->inputWrite_ = inputWrite;
    session->outputRead_ = outputRead;
    session->usingConPty_ = true;
    session->pid_ = static_cast<long>(processInfo.dwProcessId);
    return true;
}

#endif

bool TerminalSession::Start(const wxString& program, const wxArrayString& arguments,
                            const wxString& workingDirectory, wxString* error)
{
    if (IsRunning()) {
        if (error) *error = wxS("A terminal session is already running.");
        return false;
    }
    if (program.empty()) {
        if (error) *error = wxS("The terminal program is empty.");
        return false;
    }
    rawBuffer_.clear();
    lineBuffer_.clear();

#if defined(__WXMSW__)
    if (StartConPty(this, program, arguments, workingDirectory, error)) {
        usingPty_ = true;
        return true;
    }

    wxArrayString argvStrings;
    argvStrings.Add(program);
    for (const auto& argument : arguments) argvStrings.Add(argument);
    std::vector<const wxChar*> argv;
    argv.reserve(argvStrings.GetCount() + 1);
    for (const auto& argument : argvStrings) argv.push_back(argument.wx_str());
    argv.push_back(nullptr);
    process_ = new wxProcess(owner_, processId_);
    process_->Redirect();
    wxExecuteEnv environment;
    environment.cwd = workingDirectory;
    pid_ = wxExecute(argv.data(), wxEXEC_ASYNC, process_, &environment);
    if (pid_ == 0) {
        delete process_;
        process_ = nullptr;
        if (error) *error = wxString::Format(wxS("Could not start terminal program: %s."), program);
        return false;
    }
    usingPty_ = false;
    return true;
#else
    struct winsize size{};
    size.ws_col = 120;
    size.ws_row = 32;
    int slaveFd = -1;
    if (openpty(&masterFd_, &slaveFd, nullptr, nullptr, &size) != 0) {
        if (error) *error = wxString::Format(wxS("Could not allocate PTY: %s."), wxString::FromUTF8(std::strerror(errno)));
        return false;
    }
    childPid_ = fork();
    if (childPid_ < 0) {
        close(masterFd_); close(slaveFd); masterFd_ = -1;
        if (error) *error = wxS("Could not fork PTY process.");
        return false;
    }
    if (childPid_ == 0) {
        setsid();
        ioctl(slaveFd, TIOCSCTTY, 0);
        dup2(slaveFd, STDIN_FILENO);
        dup2(slaveFd, STDOUT_FILENO);
        dup2(slaveFd, STDERR_FILENO);
        if (slaveFd > STDERR_FILENO) close(slaveFd);
        if (!workingDirectory.empty()) chdir(workingDirectory.utf8_str().data());
        std::vector<std::string> values;
        values.emplace_back(program.utf8_str().data());
        for (const auto& argument : arguments) values.emplace_back(argument.utf8_str().data());
        std::vector<char*> argv;
        for (auto& value : values) argv.push_back(value.data());
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    close(slaveFd);
    fcntl(masterFd_, F_SETFL, O_NONBLOCK);
    pid_ = childPid_;
    usingPty_ = true;
    return true;
#endif
}

bool TerminalSession::IsRunning() const
{
#if defined(__WXMSW__)
    if (usingConPty_) return childProcess_ && WaitForSingleObject(static_cast<HANDLE>(childProcess_), 0) == WAIT_TIMEOUT;
    return process_ != nullptr && pid_ != 0;
#else
    if (!usingPty_ || childPid_ <= 0) return false;
    int status = 0;
    const pid_t result = waitpid(childPid_, &status, WNOHANG);
    return result == 0;
#endif
}

wxString TerminalSession::BackendName() const
{
#if defined(__WXMSW__)
    if (usingConPty_) return wxS("ConPTY");
    return wxS("pipe fallback");
#else
    return usingPty_ ? wxS("PTY") : wxS("unavailable");
#endif
}

bool TerminalSession::Write(const wxString& text)
{
    if (!IsRunning()) return false;
    const wxScopedCharBuffer utf8 = text.utf8_str();
#if defined(__WXMSW__)
    if (usingConPty_) {
        DWORD written = 0;
        return WriteFile(static_cast<HANDLE>(inputWrite_), utf8.data(), static_cast<DWORD>(utf8.length()), &written, nullptr) &&
               written == utf8.length();
    }
    wxOutputStream* stream = process_->GetOutputStream();
    stream->Write(utf8.data(), utf8.length());
    stream->Sync();
    return stream->LastWrite() == utf8.length() && stream->IsOk();
#else
    const ssize_t written = write(masterFd_, utf8.data(), utf8.length());
    return written == static_cast<ssize_t>(utf8.length());
#endif
}

bool TerminalSession::Resize(int columns, int rows)
{
    if (!IsRunning() || columns <= 0 || rows <= 0) return false;
#if defined(__WXMSW__)
    if (!usingConPty_) return false;
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    auto resize = reinterpret_cast<ResizePseudoConsoleFn>(GetProcAddress(kernel, "ResizePseudoConsole"));
    return resize && SUCCEEDED(resize(static_cast<HPCON>(pseudoConsole_), COORD{static_cast<SHORT>(columns), static_cast<SHORT>(rows)}));
#else
    struct winsize size{};
    size.ws_col = static_cast<unsigned short>(columns);
    size.ws_row = static_cast<unsigned short>(rows);
    return ioctl(masterFd_, TIOCSWINSZ, &size) == 0;
#endif
}

void TerminalSession::Stop()
{
#if defined(__WXMSW__)
    if (usingConPty_) {
        if (childProcess_) {
            TerminateProcess(static_cast<HANDLE>(childProcess_), 0);
            CloseHandle(static_cast<HANDLE>(childProcess_));
        }
        if (inputWrite_) CloseHandle(static_cast<HANDLE>(inputWrite_));
        if (outputRead_) CloseHandle(static_cast<HANDLE>(outputRead_));
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        auto close = reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(kernel, "ClosePseudoConsole"));
        if (close && pseudoConsole_) close(static_cast<HPCON>(pseudoConsole_));
        childProcess_ = nullptr; inputWrite_ = nullptr; outputRead_ = nullptr; pseudoConsole_ = nullptr;
        pid_ = 0; usingConPty_ = false; usingPty_ = false;
        return;
    }
    if (process_) {
        if (pid_ != 0) wxKill(pid_, wxSIGTERM, nullptr, wxKILL_CHILDREN);
        process_->Detach();
        delete process_;
        process_ = nullptr;
    }
    pid_ = 0;
#else
    if (childPid_ > 0) {
        kill(childPid_, SIGHUP);
        kill(childPid_, SIGTERM);
        waitpid(childPid_, nullptr, 0);
    }
    if (masterFd_ >= 0) close(masterFd_);
    masterFd_ = -1; childPid_ = 0; pid_ = 0; usingPty_ = false;
#endif
}

void TerminalSession::HandleProcessExit(long pid, int)
{
#if defined(__WXMSW__)
    if (!usingConPty_ && process_ && pid == pid_) {
        pid_ = 0;
        delete process_;
        process_ = nullptr;
    }
#else
    (void)pid;
#endif
}

wxString TerminalSession::PollRaw()
{
    if (!IsRunning()) return wxEmptyString;
    char chunk[8192];
    std::string bytes;
#if defined(__WXMSW__)
    if (usingConPty_) {
        DWORD available = 0;
        while (PeekNamedPipe(static_cast<HANDLE>(outputRead_), nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            DWORD count = 0;
            if (!ReadFile(static_cast<HANDLE>(outputRead_), chunk, sizeof(chunk), &count, nullptr) || count == 0) break;
            bytes.append(chunk, count);
        }
    } else if (process_ && process_->GetInputStream()) {
        wxInputStream* input = process_->GetInputStream();
        while (input->CanRead()) {
            input->Read(chunk, sizeof(chunk));
            const size_t count = input->LastRead();
            if (count == 0) break;
            bytes.append(chunk, count);
        }
    }
#else
    while (masterFd_ >= 0) {
        const ssize_t count = read(masterFd_, chunk, sizeof(chunk));
        if (count > 0) bytes.append(chunk, static_cast<size_t>(count));
        else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        else break;
    }
#endif
    rawBuffer_ += bytes;
    const std::string current = std::move(rawBuffer_);
    rawBuffer_.clear();
    return wxString::FromUTF8(current.data(), current.size());
}

wxArrayString TerminalSession::Poll()
{
    const wxString raw = PollRaw();
    const wxScopedCharBuffer utf8 = raw.utf8_str();
    return RawToLines(std::string(utf8.data(), utf8.length()), lineBuffer_);
}

} // namespace codium
