// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include <wx/process.h>
#include <wx/string.h>
#include <wx/window.h>

#include <memory>
#include <string>

namespace codium {

class ExtensionHostClient final {
public:
    explicit ExtensionHostClient(wxWindow* owner);
    ~ExtensionHostClient();

    ExtensionHostClient(const ExtensionHostClient&) = delete;
    ExtensionHostClient& operator=(const ExtensionHostClient&) = delete;

    bool Start(const wxString& hostScript, const wxString& nodeExecutable = wxS("node"));
    void Stop();
    bool IsRunning() const;

    bool SendRaw(const wxString& jsonLine);
    bool LoadExtension(const wxString& extensionPath);
    bool ExecuteCommand(const wxString& command);
    bool StartLanguageServer(const wxString& command);
    bool StopLanguageServer();
    bool InitializeLanguageServer(const wxString& rootUri);
    bool OpenLanguageDocument(const wxString& uri, const wxString& languageId,
                              int version, const wxString& text);
    bool ChangeLanguageDocument(const wxString& uri, int version, const wxString& text);
    bool NotifyDocumentOpened(const wxString& uri, const wxString& languageId,
                              int version, const wxString& text);
    bool NotifyDocumentChanged(const wxString& uri, const wxString& languageId,
                               int version, const wxString& text);
    bool NotifyDocumentSaved(const wxString& uri, const wxString& languageId,
                             int version, const wxString& text);
    bool RequestLanguageHover(const wxString& uri, int line, int character);
    bool RequestLanguageCompletion(const wxString& uri, int line, int character);
    bool RequestLanguageSemanticTokens(const wxString& uri);
    bool RequestLanguageDefinition(const wxString& uri, int line, int character);
    bool RequestLanguageDeclaration(const wxString& uri, int line, int character);
    bool RequestLanguageReferences(const wxString& uri, int line, int character);
    bool RequestLanguageDocumentSymbols(const wxString& uri);
    bool RequestLanguageWorkspaceSymbols(const wxString& query);
    bool RequestLanguageRename(const wxString& uri, int line, int character, const wxString& newName);
    bool RequestLanguageCodeActions(const wxString& uri, int line, int character);

    // Poll is intentionally driven by the wxWidgets event loop. It keeps the
    // native UI responsive while the optional Node.js host runs out-of-process.
    wxArrayString Poll();

private:
    wxWindow* owner_;
    wxProcess* process_;
    long pid_;
    std::string inputBuffer_;
    int nextLanguageRequestId_ = 1;
};

} // namespace codium
