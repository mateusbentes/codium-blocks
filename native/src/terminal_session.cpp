// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/terminal_session.hpp"

#include <wx/utils.h>

#include <algorithm>
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

std::wstring ResolveWindowsExecutable(const wxString& program)
{
    const std::wstring requested = program.ToStdWstring();
    const size_t slash = requested.find_last_of(L"\\/");
    const size_t dot = requested.find_last_of(L'.');
    const wchar_t* extension = dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)
        ? L".exe"
        : nullptr;
    std::vector<wchar_t> buffer(512);
    for (;;) {
        const DWORD length = SearchPathW(nullptr, requested.c_str(), extension,
                                          static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
        if (length == 0) return requested;
        if (length < buffer.size()) return std::wstring(buffer.data(), length);
        buffer.resize(length + 1);
    }
}

std::wstring QuoteWindowsArgument(const std::wstring& value)
{
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted += L'\"';
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            quoted += character;
            backslashes = 0;
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'\"';
    return quoted;
}

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

    HANDLE inputRead = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    // Keep the pipe endpoints private to the host; ConPTY receives its own
    // duplicated endpoints through CreatePseudoConsole.
    if (!CreatePipe(&inputRead, &inputWrite, nullptr, 0) ||
        !CreatePipe(&outputRead, &outputWrite, nullptr, 0)) {
        if (inputRead) CloseHandle(inputRead);
        if (inputWrite) CloseHandle(inputWrite);
        if (outputRead) CloseHandle(outputRead);
        if (outputWrite) CloseHandle(outputWrite);
        return false;
    }
    COORD size{120, 32};
    HPCON console = nullptr;
    if (FAILED(create(size, inputRead, outputWrite, 0, &console))) {
        CloseHandle(inputRead); CloseHandle(inputWrite); CloseHandle(outputRead); CloseHandle(outputWrite);
        return false;
    }

    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    auto* attributes = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attributeSize));
    if (!attributes || !InitializeProcThreadAttributeList(attributes, 1, 0, &attributeSize) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, console,
                                   sizeof(console), nullptr, nullptr)) {
        if (attributes) HeapFree(GetProcessHeap(), 0, attributes);
        auto close = reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(kernel, "ClosePseudoConsole"));
        if (close) close(console);
        CloseHandle(inputRead); CloseHandle(inputWrite); CloseHandle(outputRead); CloseHandle(outputWrite);
        return false;
    }

    const std::wstring executable = ResolveWindowsExecutable(program);
    std::wstring commandLine = QuoteWindowsArgument(executable);
    for (const auto& argument : arguments) {
        commandLine += L" ";
        commandLine += QuoteWindowsArgument(argument.ToStdWstring());
    }
    commandLine.push_back(L'\0');
    STARTUPINFOEXW startup{};
    // The extended startup structure carries the pseudo-console attribute.
    startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    // Do not let a GUI/CI parent's redirected standard handles leak into the
    // hosted process. The pseudoconsole supplies the child console handles.
    // This is also the startup shape used by established ConPTY hosts.
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nullptr;
    startup.StartupInfo.hStdOutput = nullptr;
    startup.StartupInfo.hStdError = nullptr;
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
        CloseHandle(inputRead); CloseHandle(inputWrite); CloseHandle(outputRead); CloseHandle(outputWrite);
        if (error) *error = wxS("CreateProcessW failed for ConPTY.");
        return false;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
    jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                         &jobInfo, sizeof(jobInfo)) ||
        !AssignProcessToJobObject(job, processInfo.hProcess)) {
        // Some hosted runners already place children in a non-nestable job.
        // Keep ConPTY usable there and retain direct-process termination as a fallback.
        if (job) CloseHandle(job);
        job = nullptr;
    }
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    CloseHandle(processInfo.hThread);
    session->pseudoConsole_ = console;
    session->childProcess_ = processInfo.hProcess;
    session->jobObject_ = job;
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
    pendingWrite_.clear();

#if defined(__WXMSW__)
    wxString disableConPty;
    const bool conPtyDisabled = wxGetEnv(wxS("CODIUM_BLOCKS_DISABLE_CONPTY"), &disableConPty) &&
        !disableConPty.empty() && disableConPty != wxS("0");
    if (!conPtyDisabled && StartConPty(this, program, arguments, workingDirectory, error)) {
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
        close(masterFd_);
        if (slaveFd > STDERR_FILENO) close(slaveFd);
        if (!workingDirectory.empty() && chdir(workingDirectory.utf8_str().data()) != 0) _exit(127);
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
    const int flags = fcntl(masterFd_, F_GETFL, 0);
    if (flags < 0 || fcntl(masterFd_, F_SETFL, flags | O_NONBLOCK) != 0) {
        const int flagsError = errno;
        kill(-childPid_, SIGTERM);
        while (waitpid(childPid_, nullptr, 0) < 0 && errno == EINTR) {}
        close(masterFd_);
        masterFd_ = -1;
        childPid_ = 0;
        if (error) *error = wxString::Format(wxS("Could not configure PTY: %s."), wxString::FromUTF8(std::strerror(flagsError)));
        return false;
    }
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

bool TerminalSession::FlushPendingWrite()
{
    if (pendingWrite_.empty()) return true;
#if defined(__WXMSW__)
    if (usingConPty_) {
        while (!pendingWrite_.empty()) {
            const DWORD requested = static_cast<DWORD>(std::min<size_t>(pendingWrite_.size(), MAXDWORD));
            DWORD written = 0;
            if (!WriteFile(static_cast<HANDLE>(inputWrite_), pendingWrite_.data(), requested, &written, nullptr)) {
                return false;
            }
            if (written == 0) return false;
            pendingWrite_.erase(0, written);
        }
        return true;
    }
    if (!process_ || !process_->GetOutputStream()) return false;
    while (!pendingWrite_.empty()) {
        wxOutputStream* stream = process_->GetOutputStream();
        stream->Write(pendingWrite_.data(), pendingWrite_.size());
        stream->Sync();
        const size_t written = stream->LastWrite();
        if (written == 0 || !stream->IsOk()) return false;
        pendingWrite_.erase(0, written);
    }
    return true;
#else
    if (masterFd_ < 0) return false;
    while (!pendingWrite_.empty()) {
        const ssize_t written = write(masterFd_, pendingWrite_.data(), pendingWrite_.size());
        if (written > 0) {
            pendingWrite_.erase(0, static_cast<size_t>(written));
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
        return false;
    }
    return true;
#endif
}

bool TerminalSession::Write(const wxString& text)
{
    if (!IsRunning()) return false;
    const wxScopedCharBuffer utf8 = text.utf8_str();
    if (utf8.length() == 0) return true;
    pendingWrite_.append(utf8.data(), utf8.length());
    return FlushPendingWrite();
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
        if (jobObject_) CloseHandle(static_cast<HANDLE>(jobObject_));
        if (childProcess_) {
            if (WaitForSingleObject(static_cast<HANDLE>(childProcess_), 500) == WAIT_TIMEOUT) {
                TerminateProcess(static_cast<HANDLE>(childProcess_), 0);
            }
            CloseHandle(static_cast<HANDLE>(childProcess_));
        }
        if (inputWrite_) CloseHandle(static_cast<HANDLE>(inputWrite_));
        if (outputRead_) CloseHandle(static_cast<HANDLE>(outputRead_));
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        auto close = reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(kernel, "ClosePseudoConsole"));
        if (close && pseudoConsole_) close(static_cast<HPCON>(pseudoConsole_));
        childProcess_ = nullptr; jobObject_ = nullptr; inputWrite_ = nullptr; outputRead_ = nullptr; pseudoConsole_ = nullptr;
        pid_ = 0; usingConPty_ = false; usingPty_ = false;
        pendingWrite_.clear();
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
        const pid_t processGroup = -static_cast<pid_t>(childPid_);
        if (kill(processGroup, SIGHUP) != 0 && errno == ESRCH) kill(childPid_, SIGHUP);
        if (kill(processGroup, SIGTERM) != 0 && errno == ESRCH) kill(childPid_, SIGTERM);

        bool exited = false;
        for (int attempt = 0; attempt < 50; ++attempt) {
            int status = 0;
            const pid_t result = waitpid(childPid_, &status, WNOHANG);
            if (result == childPid_ || (result < 0 && errno == ECHILD)) {
                exited = true;
                break;
            }
            if (result < 0 && errno != EINTR) break;
            usleep(10 * 1000);
        }
        if (!exited) {
            if (kill(processGroup, SIGKILL) != 0 && errno == ESRCH) kill(childPid_, SIGKILL);
            while (waitpid(childPid_, nullptr, 0) < 0 && errno == EINTR) {}
        }
    }
    if (masterFd_ >= 0) close(masterFd_);
    masterFd_ = -1; childPid_ = 0; pid_ = 0; usingPty_ = false; pendingWrite_.clear();
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
    FlushPendingWrite();
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
