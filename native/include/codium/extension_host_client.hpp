#pragma once

#include <wx/process.h>
#include <wx/string.h>
#include <wx/window.h>

#include <memory>

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

    // Poll is intentionally driven by the wxWidgets event loop. It keeps the
    // native UI responsive while the optional Node.js host runs out-of-process.
    wxArrayString Poll();

private:
    wxWindow* owner_;
    wxProcess* process_;
    long pid_;
    wxString inputBuffer_;
};

} // namespace codium
