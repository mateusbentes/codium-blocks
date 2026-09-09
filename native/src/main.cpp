#include "codium/document.hpp"
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

#include <algorithm>
#include <utility>

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

wxString JsonStringField(const wxString& line, const wxString& field)
{
    const wxString marker = wxString::Format(wxS("\"%s\":\""), field);
    const int start = line.Find(marker);
    if (start == wxNOT_FOUND) {
        return wxEmptyString;
    }

    const int valueStart = start + static_cast<int>(marker.length());
    int valueEnd = valueStart;
    bool escaped = false;
    while (valueEnd < static_cast<int>(line.length())) {
        const wxChar ch = line[valueEnd];
        if (ch == wxS('"') && !escaped) {
            break;
        }
        escaped = ch == wxS('\\') && !escaped;
        if (ch != wxS('\\')) {
            escaped = false;
        }
        ++valueEnd;
    }
    return line.Mid(valueStart, valueEnd - valueStart);
}

class MainFrame final : public wxFrame {
public:
    MainFrame()
        : wxFrame(nullptr, wxID_ANY, wxString::Format(wxS("Codium::Blocks %s"), CODIUM_BLOCKS_VERSION),
                  wxDefaultPosition, wxSize(1100, 760)),
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

        auto* fileControls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(fileControls, wxS("Open file"), [this](wxCommandEvent&) { OpenFile(); });
        AddButton(fileControls, wxS("Save file"), [this](wxCommandEvent&) { SaveFile(); });
        AddButton(fileControls, wxS("Start clangd"), [this](wxCommandEvent&) { StartLanguageServer(); });
        AddButton(fileControls, wxS("Initialize LSP"), [this](wxCommandEvent&) { InitializeLanguageServer(); });
        AddButton(fileControls, wxS("Hover"), [this](wxCommandEvent&) { RequestHover(); });
        AddButton(fileControls, wxS("Completion"), [this](wxCommandEvent&) { RequestCompletion(); });
        AddButton(fileControls, wxS("Stop language server"), [this](wxCommandEvent&) { StopLanguageServer(); });
        root->Add(fileControls, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

        auto* extensionControls = new wxBoxSizer(wxHORIZONTAL);
        extensionControls_ = extensionControls;
        AddButton(extensionControls, wxS("Start Extension Host"), [this](wxCommandEvent&) { StartHost(); });
        AddButton(extensionControls, wxS("Load demo"), [this](wxCommandEvent&) { LoadDemo(); });
        AddButton(extensionControls, wxS("Run hello.codium"), [this](wxCommandEvent&) { ExecuteDemo(); });
        AddButton(extensionControls, wxS("Install VSIX"), [this](wxCommandEvent&) { InstallVsix(); });
        AddButton(extensionControls, wxS("List extensions"), [this](wxCommandEvent&) { ListExtensions(); });
        root->Add(extensionControls, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

        editor_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 wxTE_MULTILINE | wxTE_RICH2 | wxHSCROLL);
        editor_->SetFont(wxFont(11, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        editor_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
            if (!loadingDocument_) {
                document_.SetText(editor_->GetValue());
                ++documentVersion_;
                NotifyLanguageDocumentChanged();
                UpdateTitle();
            }
        });
        root->Add(editor_, 1, wxLEFT | wxRIGHT | wxEXPAND, 10);

        log_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 180),
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);
        root->Add(log_, 0, wxALL | wxEXPAND, 10);
        SetSizer(root);
        Centre();

        timer_.Start(50);
        AppendLog(wxS("Ready. The native core does not load Electron."));
        AppendLog(wxS("Open a file or start the host to use extensions and language tooling."));
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

    void UpdateTitle()
    {
        const wxString name = document_.IsUntitled()
            ? wxS("Untitled")
            : wxFileName(document_.Path()).GetFullName();
        SetTitle(wxString::Format(wxS("%s%s — Codium::Blocks %s"),
                                  name, document_.IsDirty() ? wxS(" *") : wxEmptyString,
                                  CODIUM_BLOCKS_VERSION));
    }

    void OpenFile()
    {
        wxFileDialog dialog(this, wxS("Open source file"), wxEmptyString, wxEmptyString,
                            wxS("All files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }

        codium::Document loaded;
        wxString error;
        if (!loaded.Load(dialog.GetPath(), &error)) {
            AppendLog(wxS("Error: ") + error);
            return;
        }

        loadingDocument_ = true;
        document_ = std::move(loaded);
        editor_->SetValue(document_.Text());
        documentVersion_ = 1;
        loadingDocument_ = false;
        UpdateTitle();
        AppendLog(wxS("Opened: ") + document_.Path());
        NotifyLanguageDocumentOpened();
    }

    void SaveFile()
    {
        if (document_.IsUntitled()) {
            wxFileDialog dialog(this, wxS("Save source file"), wxEmptyString, wxEmptyString,
                                wxS("All files (*.*)|*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (dialog.ShowModal() != wxID_OK) {
                return;
            }
            document_ = codium::Document(dialog.GetPath());
        }

        document_.SetText(editor_->GetValue());
        wxString error;
        if (!document_.Save(&error)) {
            AppendLog(wxS("Error: ") + error);
            return;
        }
        document_.MarkClean();
        UpdateTitle();
        AppendLog(wxS("Saved: ") + document_.Path());
        NotifyLanguageDocumentChanged();
    }

    wxString HostScript() const
    {
        return projectRoot_ + wxFILE_SEP_PATH + wxS("extension-host/src/host.mjs");
    }

    wxString DemoExtension() const
    {
        return projectRoot_ + wxFILE_SEP_PATH + wxS("extensions/hello-codium");
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

    void StartLanguageServer()
    {
        if (!host_.IsRunning()) {
            StartHost();
        }
        if (host_.StartLanguageServer(wxS("clangd"))) {
            AppendLog(wxS("Request sent: start clangd through the Extension Host."));
        }
    }

    void InitializeLanguageServer()
    {
        if (!host_.IsRunning()) {
            StartHost();
        }
        if (host_.InitializeLanguageServer(ProjectRootUri())) {
            languageServerInitialized_ = true;
            AppendLog(wxS("Request sent: initialize the language server."));
            NotifyLanguageDocumentOpened();
        }
    }

    void NotifyLanguageDocumentOpened()
    {
        if (!languageServerInitialized_ || document_.IsUntitled()) {
            return;
        }
        host_.OpenLanguageDocument(DocumentUri(), wxS("plaintext"), documentVersion_, document_.Text());
    }

    void NotifyLanguageDocumentChanged()
    {
        if (!languageServerInitialized_ || document_.IsUntitled()) {
            return;
        }
        host_.ChangeLanguageDocument(DocumentUri(), documentVersion_, document_.Text());
    }

    wxString ProjectRootUri() const
    {
        wxString path = projectRoot_;
        path.Replace(wxS("\\"), wxS("/"));
        return wxString::Format(wxS("file://%s"), path);
    }

    wxString DocumentUri() const
    {
        wxString path = document_.Path();
        path.Replace(wxS("\\"), wxS("/"));
        return wxString::Format(wxS("file://%s"), path);
    }

    int CurrentEditorLine() const
    {
        const long position = editor_->GetInsertionPoint();
        return static_cast<int>(editor_->GetValue().Left(position).Freq(wxS('\n')));
    }

    int CurrentEditorCharacter() const
    {
        const long position = editor_->GetInsertionPoint();
        const wxString before = editor_->GetValue().Left(position);
        const int lineBreak = before.Find(wxS('\n'), true);
        return lineBreak == wxNOT_FOUND ? static_cast<int>(position) :
                                          static_cast<int>(position - lineBreak - 1);
    }

    void RequestHover()
    {
        if (document_.IsUntitled()) {
            AppendLog(wxS("Open a document before requesting hover information."));
            return;
        }
        if (host_.RequestLanguageHover(DocumentUri(), CurrentEditorLine(), CurrentEditorCharacter())) {
            AppendLog(wxS("Request sent: textDocument/hover."));
        }
    }

    void RequestCompletion()
    {
        if (document_.IsUntitled()) {
            AppendLog(wxS("Open a document before requesting completion."));
            return;
        }
        if (host_.RequestLanguageCompletion(DocumentUri(), CurrentEditorLine(), CurrentEditorCharacter())) {
            AppendLog(wxS("Request sent: textDocument/completion."));
        }
    }

    void StopLanguageServer()
    {
        if (host_.StopLanguageServer()) {
            AppendLog(wxS("Request sent: stop the language server."));
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

    void RegisterContributedCommand(const wxString& line)
    {
        const wxString command = JsonStringField(line, wxS("command"));
        const wxString title = JsonStringField(line, wxS("title"));
        if (command.empty() || title.empty() || !extensionControls_) {
            return;
        }

        AddButton(extensionControls_, title, [this, command](wxCommandEvent&) {
            if (host_.ExecuteCommand(command)) {
                AppendLog(wxS("Request sent: run contributed command ") + command + wxS("."));
            }
        });
        extensionControls_->Layout();
        Layout();
        AppendLog(wxS("Registered extension command: ") + command);
    }

    void OnTimer(wxTimerEvent&)
    {
        for (const auto& line : host_.Poll()) {
            AppendLog(wxS("host> ") + line);
            if (line.Find(wxS("\"event\":\"languageServerMessage\"")) != wxNOT_FOUND) {
                AppendLog(wxS("LSP message received."));
            }
            if (line.Find(wxS("\"event\":\"diagnostics\"")) != wxNOT_FOUND) {
                AppendLog(wxS("LSP diagnostics updated."));
            }
            if (line.Find(wxS("\"event\":\"languageServerResult\"")) != wxNOT_FOUND) {
                if (line.Find(wxS("\"method\":\"textDocument/hover\"")) != wxNOT_FOUND) {
                    AppendLog(wxS("LSP hover result received."));
                } else if (line.Find(wxS("\"method\":\"textDocument/completion\"")) != wxNOT_FOUND) {
                    AppendLog(wxS("LSP completion result received."));
                } else if (line.Find(wxS("\"method\":\"initialize\"")) != wxNOT_FOUND) {
                    AppendLog(wxS("LSP initialized."));
                }
            }
            if (line.Find(wxS("\"event\":\"contribution\"")) != wxNOT_FOUND &&
                line.Find(wxS("\"kind\":\"command\"")) != wxNOT_FOUND) {
                RegisterContributedCommand(line);
            }
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
    codium::Document document_;
    wxBoxSizer* extensionControls_ = nullptr;
    wxTextCtrl* editor_ = nullptr;
    wxTextCtrl* log_ = nullptr;
    wxTimer timer_;
    bool loadingDocument_ = false;
    int documentVersion_ = 1;
    bool languageServerInitialized_ = false;

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
