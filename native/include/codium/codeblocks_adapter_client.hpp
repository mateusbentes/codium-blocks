// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include "codium/codeblocks_host.hpp"

#include <wx/process.h>
#include <wx/string.h>
#include <wx/window.h>

#include <string>
#include <vector>

namespace codium {

class CodeBlocksAdapterClient final {
public:
    explicit CodeBlocksAdapterClient(wxWindow* owner);
    ~CodeBlocksAdapterClient();

    CodeBlocksAdapterClient(const CodeBlocksAdapterClient&) = delete;
    CodeBlocksAdapterClient& operator=(const CodeBlocksAdapterClient&) = delete;

    bool Start(const wxString& executable,
               const wxArrayString& arguments,
               const wxString& workingDirectory,
               const CodeBlocksHostConfiguration& configuration,
               wxString* error = nullptr);
    void Stop();
    bool IsRunning() const;
    bool IsReady() const { return ready_; }
    bool HandshakeReceived() const { return handshakeReceived_; }

    bool SendRaw(const wxString& jsonLine);
    bool OpenProject(const wxString& projectFile);
    bool BuildTarget(const wxString& projectFile, const wxString& target, const wxString& configuration);
    bool DebugProject(const wxString& projectFile, const wxString& target, bool breakOnEntry);
    bool ContinueDebug();
    bool PauseDebug();
    bool StopDebug();
    bool RequestDebugSnapshot(const wxString& dataKind = wxS("state"),
                              const wxString& expression = wxString());

    wxArrayString Poll();
    std::vector<CodeBlocksHostEvent> PollEvents();

    int ContractMajor() const { return contractMajor_; }
    int ContractMinor() const { return contractMinor_; }
    int SdkMajor() const { return sdkMajor_; }
    int SdkMinor() const { return sdkMinor_; }
    int SdkRelease() const { return sdkRelease_; }
    const wxArrayString& Capabilities() const { return capabilities_; }
    const wxString& DebuggerProviderIdentity() const { return debuggerProviderIdentity_; }
    const wxString& DebuggerProviderSourceRevision() const { return debuggerProviderSourceRevision_; }
    const wxString& DebuggerProviderAbiIdentity() const { return debuggerProviderAbiIdentity_; }
    const wxString& LastErrorCode() const { return lastErrorCode_; }
    const wxString& LastErrorMessage() const { return lastErrorMessage_; }

private:
    bool StartHandshake(const CodeBlocksHostConfiguration& configuration);
    void ProcessProtocolLine(const wxString& line, std::vector<CodeBlocksHostEvent>* events);

    wxWindow* owner_;
    wxProcess* process_ = nullptr;
    long pid_ = 0;
    std::string inputBuffer_;
    bool ready_ = false;
    bool handshakeReceived_ = false;
    int contractMajor_ = 0;
    int contractMinor_ = 0;
    int sdkMajor_ = 0;
    int sdkMinor_ = 0;
    int sdkRelease_ = 0;
    wxArrayString capabilities_;
    wxString debuggerProviderIdentity_;
    wxString debuggerProviderSourceRevision_;
    wxString debuggerProviderAbiIdentity_;
    wxString lastErrorCode_;
    wxString lastErrorMessage_;
};

} // namespace codium
