#include "codium/extension_host_client.hpp"
#include "codium/vsix_manager.hpp"

#include <wx/button.h>
#include <wx/dir.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/wx.h>

namespace {

wxString ParentDirectory(wxString path)
{
    while (path.EndsWith(wxFILE_SEP_PATH)) {
        path.RemoveLast();
    }
    const int separator = path.Find(wxFILE_SEP_PATH, true);
    return separator == wxNOT_FOUND ? wxString(wxEmptyString) : path.Left(separator);
}

wxString DetectProjectRoot()
{
    const wxString executable = wxStandardPaths::Get().GetExecutablePath();
    const wxString executableDirectory = wxFileName(executable).GetPath();
    if (wxDirExists(executableDirectory + wxFILE_SEP_PATH + wxS("extension-host"))) {
        return executableDirectory;
    }

    const wxString parent = ParentDirectory(executableDirectory);
    if (wxDirExists(parent + wxFILE_SEP_PATH + wxS("extension-host"))) {
        return parent;
    }

    // Development fallback for IDEs that launch the binary from a build tree.
    return wxString(CODIUM_BLOCKS_SOURCE_DIR);
}

class MainFrame final : public wxFrame {
public:
    MainFrame()
        : wxFrame(nullptr, wxID_ANY, wxString::Format(wxS("Codium::Blocks %s"), CODIUM_BLOCKS_VERSION),
                  wxDefaultPosition, wxSize(980, 680)),
          projectRoot_(DetectProjectRoot()),
          host_(this),
          extensions_(projectRoot_ + wxFILE_SEP_PATH + wxS("extensions-installed")),
          timer_(this)
    {
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* title = new wxStaticText(this, wxID_ANY,
            wxS("Native C++/wxWidgets IDE — optional Node.js Extension Host — no Electron"));
        title->SetFont(title->GetFont().Bold());
        root->Add(title, 0, wxALL | wxEXPAND, 10);

        auto* controls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(controls, wxS("Start Extension Host"), [this](wxCommandEvent&) { StartHost(); });
        AddButton(controls, wxS("Load demo"), [this](wxCommandEvent&) { LoadDemo(); });
        AddButton(controls, wxS("Run hello.codium"), [this](wxCommandEvent&) { ExecuteDemo(); });
        AddButton(controls, wxS("Install VSIX"), [this](wxCommandEvent&) { InstallVsix(); });
        AddButton(controls, wxS("List extensions"), [this](wxCommandEvent&) { ListExtensions(); });
        root->Add(controls, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

        log_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);
        root->Add(log_, 1, wxALL | wxEXPAND, 10);
        SetSizer(root);
        Centre();

        timer_.Start(50);
        AppendLog(wxS("Ready. The native core does not load Electron."));
        AppendLog(wxS("Start the host and load the demo extension to test IPC."));
    }

private:
    template <typename Handler>
    void AddButton(wxSizer* sizer, const wxString& label, Handler&& handler)
    {
        auto* button = new wxButton(this, wxID_ANY, label);
        button->Bind(wxEVT_BUTTON, std::forward<Handler>(handler));
        sizer->Add(button, 0, wxRIGHT, 6);
    }

    void AppendLog(const wxString& line)
    {
        if (log_) {
            log_->AppendText(line + wxS("\n"));
        }
    }

    wxString HostScript() const
    {
        return projectRoot_ + wxFILE_SEP_PATH +
               wxS("extension-host/src/host.mjs");
    }

    wxString DemoExtension() const
    {
        return projectRoot_ + wxFILE_SEP_PATH +
               wxS("extensions/hello-codium");
    }

    void StartHost()
    {
        if (host_.IsRunning()) {
            AppendLog(wxS("Extension Host is already running."));
            return;
        }
        if (host_.Start(HostScript())) {
            AppendLog(wxS("Node.js Extension Host started in a separate process."));
        } else {
            AppendLog(wxS("Could not start the Extension Host."));
        }
    }

    void LoadDemo()
    {
        if (!host_.IsRunning()) {
            AppendLog(wxS("Start the Extension Host first."));
            return;
        }
        if (host_.LoadExtension(DemoExtension())) {
            AppendLog(wxS("Request sent: load hello-codium."));
        }
    }

    void ExecuteDemo()
    {
        if (!host_.IsRunning()) {
            AppendLog(wxS("Start the Extension Host first."));
            return;
        }
        if (host_.ExecuteCommand(wxS("hello.codium"))) {
            AppendLog(wxS("Request sent: run hello.codium."));
        }
    }

    void InstallVsix()
    {
        wxFileDialog dialog(this, wxS("Choose a VSIX extension"), wxEmptyString, wxEmptyString,
                            wxS("VS Code extensions (*.vsix)|*.vsix"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }

        wxString message;
        if (extensions_.Install(dialog.GetPath(), &message)) {
            AppendLog(message);
        } else {
            AppendLog(wxS("Error: ") + message);
        }
    }

    void ListExtensions()
    {
        const wxArrayString installed = extensions_.ListInstalled();
        if (installed.IsEmpty()) {
            AppendLog(wxS("No VSIX extensions are installed locally."));
            return;
        }
        for (const auto& name : installed) {
            AppendLog(wxS("Extension: ") + name);
        }
    }

    void OnTimer(wxTimerEvent&)
    {
        for (const auto& line : host_.Poll()) {
            AppendLog(wxS("host> ") + line);
        }
    }

    void OnClose(wxCloseEvent& event)
    {
        host_.Stop();
        event.Skip();
    }

    wxString projectRoot_;
    codium::ExtensionHostClient host_;
    codium::VsixManager extensions_;
    wxTextCtrl* log_ = nullptr;
    wxTimer timer_;

    wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_TIMER(wxID_ANY, MainFrame::OnTimer)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

class CodiumBlocksApp final : public wxApp {
public:
    bool OnInit() override
    {
        auto* frame = new MainFrame();
        frame->Show();
        return true;
    }
};

} // namespace

wxIMPLEMENT_APP(CodiumBlocksApp);
