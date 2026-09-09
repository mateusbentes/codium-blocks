#include "codium/document.hpp"
#include "codium/extension_host_client.hpp"
#include "codium/project_config.hpp"
#include "codium/task_runner.hpp"
#include "codium/terminal_session.hpp"
#include "codium/terminal_screen.hpp"
#include "codium/terminal_profile.hpp"
#include "codium/dap_client.hpp"
#include "codium/debug_model.hpp"
#include "codium/native_contributions.hpp"
#include "codium/extension_registry.hpp"
#include "codium/problem_model.hpp"
#include "codium/vsix_manager.hpp"
#include "codium/workspace.hpp"

#include <wx/button.h>
#include <wx/choicdlg.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dcclient.h>
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/textdlg.h>
#include <wx/timer.h>
#include <wx/treectrl.h>
#include <wx/wx.h>

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

enum : int {
    ID_START_CLANGD = wxID_HIGHEST + 1,
    ID_INITIALIZE_LSP,
    ID_HOVER,
    ID_COMPLETION,
    ID_STOP_LSP,
    ID_START_HOST,
    ID_LOAD_DEMO,
    ID_RUN_DEMO,
    ID_INSTALL_VSIX,
    ID_LIST_EXTENSIONS,
    ID_OPEN_WORKSPACE,
    ID_COMMAND_PALETTE,
    ID_BUILD_PROJECT,
    ID_RUN_TASK,
    ID_STOP_TASK,
    ID_TASK_PROCESS = wxID_HIGHEST + 500,
    ID_START_TERMINAL,
    ID_SEND_TERMINAL,
    ID_STOP_TERMINAL,
    ID_SELECT_SHELL,
    ID_TERMINAL_PROCESS,
    ID_START_DEBUG,
    ID_DEBUG_INITIALIZE,
    ID_DEBUG_LAUNCH,
    ID_DEBUG_CONTINUE,
    ID_DEBUG_PAUSE,
    ID_STOP_DEBUG,
    ID_DAP_PROCESS,
};

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

int JsonIntField(const wxString& line, const wxString& field, int fallback = 0)
{
    const wxString marker = wxString::Format(wxS("\"%s\":"), field);
    const int start = line.Find(marker);
    if (start == wxNOT_FOUND) return fallback;
    int index = start + static_cast<int>(marker.length());
    while (index < static_cast<int>(line.length()) && (line[index] == wxChar(' ') || line[index] == wxChar('\t'))) ++index;
    int sign = 1;
    if (index < static_cast<int>(line.length()) && line[index] == wxChar('-')) { sign = -1; ++index; }
    int value = 0;
    bool found = false;
    while (index < static_cast<int>(line.length()) && line[index] >= wxChar('0') && line[index] <= wxChar('9')) {
        value = value * 10 + static_cast<int>(line[index] - wxChar('0'));
        found = true;
        ++index;
    }
    return found ? sign * value : fallback;
}

std::vector<int> JsonIntFields(const wxString& line, const wxString& field)
{
    std::vector<int> values;
    const wxString marker = wxString::Format(wxS("\"%s\":"), field);
    int searchFrom = 0;
    while (searchFrom < static_cast<int>(line.length())) {
        const int relativeStart = line.Mid(searchFrom).Find(marker);
        if (relativeStart == wxNOT_FOUND) break;
        int index = searchFrom + relativeStart + static_cast<int>(marker.length());
        while (index < static_cast<int>(line.length()) && (line[index] == wxChar(' ') || line[index] == wxChar('\t'))) ++index;
        int value = 0;
        bool found = false;
        while (index < static_cast<int>(line.length()) && line[index] >= wxChar('0') && line[index] <= wxChar('9')) {
            value = value * 10 + static_cast<int>(line[index] - wxChar('0'));
            found = true;
            ++index;
        }
        if (found) values.push_back(value);
        searchFrom = index + 1;
    }
    return values;
}

wxString JsonEscape(const wxString& value)
{
    wxString result;
    for (const auto character : value) {
        if (character == wxChar('\\')) result += wxS("\\\\");
        else if (character == wxChar('"')) result += wxS("\\\"");
        else if (character == wxChar('\n')) result += wxS("\\n");
        else if (character == wxChar('\r')) result += wxS("\\r");
        else result += character;
    }
    return result;
}

wxArrayString JsonStringFields(const wxString& line, const wxString& field)
{
    wxArrayString values;
    const wxString marker = wxString::Format(wxS("\"%s\":\""), field);
    int searchFrom = 0;
    while (searchFrom < static_cast<int>(line.length())) {
        const int relativeStart = line.Mid(searchFrom).Find(marker);
        if (relativeStart == wxNOT_FOUND) break;
        const int start = searchFrom + relativeStart;
        const int valueStart = start + static_cast<int>(marker.length());
        int valueEnd = valueStart;
        while (valueEnd < static_cast<int>(line.length()) && line[valueEnd] != wxChar('"')) ++valueEnd;
        values.Add(line.Mid(valueStart, valueEnd - valueStart));
        searchFrom = valueEnd + 1;
    }
    return values;
}

wxString LanguageIdForPath(const wxString& path)
{
    const wxString extension = wxFileName(path).GetExt().Lower();
    if (extension == wxS("c") || extension == wxS("h")) return wxS("c");
    if (extension == wxS("cc") || extension == wxS("cpp") || extension == wxS("cxx") ||
        extension == wxS("hh") || extension == wxS("hpp") || extension == wxS("hxx")) return wxS("cpp");
    if (extension == wxS("py")) return wxS("python");
    if (extension == wxS("rs")) return wxS("rust");
    if (extension == wxS("go")) return wxS("go");
    if (extension == wxS("java")) return wxS("java");
    if (extension == wxS("js") || extension == wxS("mjs") || extension == wxS("cjs")) return wxS("javascript");
    if (extension == wxS("ts") || extension == wxS("tsx")) return wxS("typescript");
    if (extension == wxS("jsx")) return wxS("javascriptreact");
    if (extension == wxS("json")) return wxS("json");
    if (extension == wxS("html")) return wxS("html");
    if (extension == wxS("css")) return wxS("css");
    if (extension == wxS("md")) return wxS("markdown");
    if (extension == wxS("yaml") || extension == wxS("yml")) return wxS("yaml");
    if (extension == wxS("cmake")) return wxS("cmake");
    return wxS("plaintext");
}

wxString DefaultShell()
{
#if defined(__WXMSW__)
    wxString shell;
    if (wxGetEnv(wxS("ComSpec"), &shell) && !shell.empty()) return shell;
    return wxS("cmd.exe");
#else
    wxString shell;
    if (wxGetEnv(wxS("SHELL"), &shell) && !shell.empty()) return shell;
    return wxS("/bin/sh");
#endif
}

class FileTreeData final : public wxTreeItemData {
public:
    explicit FileTreeData(wxString path)
        : path_(std::move(path))
    {
    }

    const wxString& Path() const { return path_; }

private:
    wxString path_;
};

struct DebugFrameLocation final {
    int id = 0;
    int line = 0;
    int character = 0;
    wxString path;
};

class MainFrame final : public wxFrame {
public:
    MainFrame()
        : wxFrame(nullptr, wxID_ANY, wxString::Format(wxS("Codium::Blocks %s"), CODIUM_BLOCKS_VERSION),
                  wxDefaultPosition, wxSize(1100, 760)),
          projectRoot_(DetectProjectRoot()),
          host_(this),
          taskRunner_(this, ID_TASK_PROCESS),
          terminal_(this, ID_TERMINAL_PROCESS),
          terminalScreen_(120, 32),
          dap_(this, ID_DAP_PROCESS),
          terminalShell_(DefaultShell()),
          extensions_(projectRoot_ + wxFILE_SEP_PATH + wxS("extensions-installed")),
          timer_(this)
    {
        BuildMenuBar();
        treeRegistry_.Register(wxS("workspace"), wxS("Workspace"));
        customEditors_.Register(wxS("json"), wxS("JSON text editor"));
        customEditors_.Register(wxS("md"), wxS("Markdown text editor"));
        terminalProfile_ = codium::TerminalProfileStore::Load();
        if (!terminalProfile_.shell.empty()) terminalShell_ = terminalProfile_.shell;
        terminalColumns_ = std::max(20, terminalProfile_.columns);
        terminalRows_ = std::max(4, terminalProfile_.rows);
        for (const auto& command : terminalProfile_.history) terminalHistory_.push_back(command);
        historyIndex_ = terminalHistory_.size();
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* title = new wxStaticText(this, wxID_ANY,
            wxS("Native C++/wxWidgets IDE — optional Node.js Extension Host — no Electron"));
        title->SetFont(title->GetFont().Bold());
        root->Add(title, 0, wxALL | wxEXPAND, 10);

        auto* workspaceControls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(workspaceControls, wxS("Open workspace"), [this](wxCommandEvent&) { OpenWorkspace(); });
        AddButton(workspaceControls, wxS("Build project"), [this](wxCommandEvent&) { BuildProject(); });
        AddButton(workspaceControls, wxS("Run task"), [this](wxCommandEvent&) { RunSelectedTask(); });
        AddButton(workspaceControls, wxS("Stop task"), [this](wxCommandEvent&) { StopTask(); });
        AddButton(workspaceControls, wxS("Refresh SCM"), [this](wxCommandEvent&) { RefreshScm(); });
        root->Add(workspaceControls, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

        fileTree_ = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 150),
                                   wxTR_DEFAULT_STYLE | wxTR_SINGLE);
        fileTree_->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& event) { OpenTreeItem(event); });
        root->Add(fileTree_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);

        root->Add(new wxStaticText(this, wxID_ANY, wxS("Tree Views")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        treeViewsList_ = new wxListBox(this, wxID_ANY);
        root->Add(treeViewsList_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);
        root->Add(new wxStaticText(this, wxID_ANY, wxS("Source Control")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        scm_ = new wxListBox(this, wxID_ANY);
        root->Add(scm_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);
        root->Add(new wxStaticText(this, wxID_ANY, wxS("Custom editors")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        customEditorsView_ = new wxListBox(this, wxID_ANY);
        for (const auto& entry : customEditors_.Entries()) customEditorsView_->Append(entry);
        root->Add(customEditorsView_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);

        taskList_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 60));
        root->Add(taskList_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);

        auto* terminalControls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(terminalControls, wxS("Start terminal"), [this](wxCommandEvent&) { StartTerminal(); });
        AddButton(terminalControls, wxS("Send input"), [this](wxCommandEvent&) { SendTerminalInput(); });
        AddButton(terminalControls, wxS("Stop terminal"), [this](wxCommandEvent&) { StopTerminal(); });
        AddButton(terminalControls, wxS("Select shell"), [this](wxCommandEvent&) { SelectShell(); });
        root->Add(terminalControls, 0, wxLEFT | wxRIGHT | wxTOP, 10);
        terminalInput_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 28));
        terminalInput_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
            if (event.GetKeyCode() == WXK_UP && !terminalHistory_.empty()) {
                if (historyIndex_ > 0) --historyIndex_;
                terminalInput_->SetValue(terminalHistory_[historyIndex_]);
                terminalInput_->SetInsertionPointEnd();
            } else if (event.GetKeyCode() == WXK_DOWN && !terminalHistory_.empty()) {
                if (historyIndex_ + 1 < terminalHistory_.size()) {
                    ++historyIndex_;
                    terminalInput_->SetValue(terminalHistory_[historyIndex_]);
                } else {
                    historyIndex_ = terminalHistory_.size();
                    terminalInput_->Clear();
                }
                terminalInput_->SetInsertionPointEnd();
            } else {
                event.Skip();
            }
        });
        root->Add(terminalInput_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);
        root->Add(new wxStaticText(this, wxID_ANY, wxS("Terminal")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        terminalOutput_ = new wxRichTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 120),
                                             wxRE_MULTILINE | wxRE_READONLY | wxHSCROLL);
        terminalOutput_->SetBackgroundColour(wxColour(20, 20, 20));
        terminalOutput_->BeginTextColour(wxColour(230, 230, 230));
        terminalOutput_->EndTextColour();
        terminalOutput_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            if (terminal_.IsRunning()) {
                const wxSize size = terminalOutput_->GetClientSize();
                const int columns = std::max(20, size.GetWidth() / 8);
                const int rows = std::max(4, size.GetHeight() / 16);
                terminalColumns_ = columns;
                terminalRows_ = rows;
                terminal_.Resize(columns, rows);
                terminalScreen_.Resize(columns, rows);
                RenderTerminalScreen();
            }
            event.Skip();
        });
        terminalOutput_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) { HandleTerminalKey(event); });
        terminalOutput_->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) { BeginTerminalSelection(event); });
        terminalOutput_->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) { UpdateTerminalSelection(event); });
        terminalOutput_->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) { EndTerminalSelection(event); });
        terminalOutput_->Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent& event) { BeginTerminalSelection(event); });
        terminalOutput_->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent& event) { EndTerminalSelection(event); });
        terminalOutput_->Bind(wxEVT_MIDDLE_DOWN, [this](wxMouseEvent& event) { BeginTerminalSelection(event); });
        terminalOutput_->Bind(wxEVT_MIDDLE_UP, [this](wxMouseEvent& event) { EndTerminalSelection(event); });
        terminalOutput_->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& event) { HandleTerminalWheel(event); });
        root->Add(terminalOutput_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);

        auto* debugControls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(debugControls, wxS("Start debug adapter"), [this](wxCommandEvent&) { StartDebugAdapter(); });
        AddButton(debugControls, wxS("Initialize debug"), [this](wxCommandEvent&) { InitializeDebug(); });
        AddButton(debugControls, wxS("Launch"), [this](wxCommandEvent&) { LaunchDebug(); });
        AddButton(debugControls, wxS("Toggle breakpoint"), [this](wxCommandEvent&) { ToggleBreakpoint(); });
        AddButton(debugControls, wxS("Threads"), [this](wxCommandEvent&) { RequestDebugThreads(); });
        AddButton(debugControls, wxS("Stack trace"), [this](wxCommandEvent&) { RequestStackTrace(); });
        AddButton(debugControls, wxS("Evaluate"), [this](wxCommandEvent&) { EvaluateDebugExpression(); });
        AddButton(debugControls, wxS("Add watch"), [this](wxCommandEvent&) { AddWatch(); });
        AddButton(debugControls, wxS("Source map"), [this](wxCommandEvent&) { ConfigureSourceMap(); });
        AddButton(debugControls, wxS("Continue"), [this](wxCommandEvent&) { ContinueDebug(); });
        AddButton(debugControls, wxS("Pause"), [this](wxCommandEvent&) { PauseDebug(); });
        AddButton(debugControls, wxS("Stop debug"), [this](wxCommandEvent&) { StopDebug(); });
        root->Add(debugControls, 0, wxLEFT | wxRIGHT | wxTOP, 10);

        root->Add(new wxStaticText(this, wxID_ANY, wxS("Breakpoints")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        breakpoints_ = new wxListBox(this, wxID_ANY);
        root->Add(breakpoints_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);
        root->Add(new wxStaticText(this, wxID_ANY, wxS("Threads")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        debugThreads_ = new wxListBox(this, wxID_ANY);
        root->Add(debugThreads_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);
        root->Add(new wxStaticText(this, wxID_ANY, wxS("Call stack")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        callStack_ = new wxListBox(this, wxID_ANY);
        callStack_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) { GoToStackFrame(event.GetSelection()); });
        root->Add(callStack_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);
        root->Add(new wxStaticText(this, wxID_ANY, wxS("Variables / evaluate")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        variables_ = new wxListBox(this, wxID_ANY);
        root->Add(variables_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);
        root->Add(new wxStaticText(this, wxID_ANY, wxS("Watches")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        watches_ = new wxListBox(this, wxID_ANY);
        root->Add(watches_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);
        root->Add(new wxStaticText(this, wxID_ANY, wxS("Adapter capabilities")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        debugCapabilities_ = new wxListBox(this, wxID_ANY);
        root->Add(debugCapabilities_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);
        debugConsole_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 70),
                                       wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
        root->Add(debugConsole_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);

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
        AddButton(extensionControls, wxS("Search Open VSX"), [this](wxCommandEvent&) { SearchOpenVsx(); });
        root->Add(extensionControls, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

        notebook_ = new wxNotebook(this, wxID_ANY);
        editor_ = new wxTextCtrl(notebook_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 wxTE_MULTILINE | wxTE_RICH2 | wxHSCROLL);
        editor_->SetFont(wxFont(11, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        editor_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
            OnEditorChanged(static_cast<wxTextCtrl*>(event.GetEventObject()));
        });
        notebook_->AddPage(editor_, wxS("Untitled"), true);
        tabPaths_.push_back(wxEmptyString);
        notebook_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& event) {
            SwitchToTab(static_cast<size_t>(event.GetSelection()));
            event.Skip();
        });
        root->Add(notebook_, 1, wxLEFT | wxRIGHT | wxEXPAND, 10);

        auto* diagnosticsLabel = new wxStaticText(this, wxID_ANY, wxS("Diagnostics"));
        root->Add(diagnosticsLabel, 0, wxLEFT | wxRIGHT | wxTOP, 10);
        diagnostics_ = new wxListBox(this, wxID_ANY);
        diagnostics_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) { GoToProblem(event.GetSelection()); });
        root->Add(diagnostics_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);

        auto* hoverLabel = new wxStaticText(this, wxID_ANY, wxS("Hover / language-server result"));
        root->Add(hoverLabel, 0, wxLEFT | wxRIGHT | wxTOP, 10);
        hover_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 70),
                                wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
        root->Add(hover_, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);

        auto* completionLabel = new wxStaticText(this, wxID_ANY, wxS("Completion items"));
        root->Add(completionLabel, 0, wxLEFT | wxRIGHT | wxTOP, 10);
        completion_ = new wxListBox(this, wxID_ANY);
        root->Add(completion_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

        root->Add(new wxStaticText(this, wxID_ANY, wxS("Output / task log")), 0, wxLEFT | wxRIGHT | wxTOP, 10);
        log_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 180),
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);
        root->Add(log_, 0, wxALL | wxEXPAND, 10);
        SetSizer(root);
        Centre();

        Bind(wxEVT_END_PROCESS, [this](wxProcessEvent& event) { OnTaskFinished(event); }, ID_TASK_PROCESS);
        Bind(wxEVT_END_PROCESS, [this](wxProcessEvent& event) { OnTerminalFinished(event); }, ID_TERMINAL_PROCESS);
        Bind(wxEVT_END_PROCESS, [this](wxProcessEvent& event) { OnDebugFinished(event); }, ID_DAP_PROCESS);

        timer_.Start(50);
        AppendLog(wxS("Ready. The native core does not load Electron."));
        AppendLog(wxS("Open a file or start the host to use extensions and language tooling."));
    }

private:
    void BuildMenuBar()
    {
        auto* menuBar = new wxMenuBar();

        auto* fileMenu = new wxMenu();
        fileMenu->Append(ID_COMMAND_PALETTE, wxS("Command Palette...\tCtrl+Shift+P"));
        fileMenu->AppendSeparator();
        fileMenu->Append(ID_OPEN_WORKSPACE, wxS("Open workspace..."));
        fileMenu->AppendSeparator();
        fileMenu->Append(wxID_OPEN, wxS("Open file\tCtrl+O"));
        fileMenu->Append(wxID_SAVE, wxS("Save file\tCtrl+S"));
        fileMenu->AppendSeparator();
        fileMenu->Append(wxID_EXIT, wxS("Exit\tCtrl+Q"));
        menuBar->Append(fileMenu, wxS("&File"));

        auto* languageMenu = new wxMenu();
        languageMenu->Append(ID_START_CLANGD, wxS("Start clangd"));
        languageMenu->Append(ID_INITIALIZE_LSP, wxS("Initialize language server"));
        languageMenu->Append(ID_HOVER, wxS("Request hover\tF1"));
        languageMenu->Append(ID_COMPLETION, wxS("Request completion\tCtrl+Space"));
        languageMenu->Append(ID_STOP_LSP, wxS("Stop language server"));
        menuBar->Append(languageMenu, wxS("&Language"));

        auto* buildMenu = new wxMenu();
        buildMenu->Append(ID_BUILD_PROJECT, wxS("Build project"));
        buildMenu->Append(ID_RUN_TASK, wxS("Run selected task"));
        buildMenu->Append(ID_STOP_TASK, wxS("Stop task"));
        menuBar->Append(buildMenu, wxS("&Build"));

        auto* terminalMenu = new wxMenu();
        terminalMenu->Append(ID_START_TERMINAL, wxS("Start terminal"));
        terminalMenu->Append(ID_SEND_TERMINAL, wxS("Send input"));
        terminalMenu->Append(ID_STOP_TERMINAL, wxS("Stop terminal"));
        terminalMenu->Append(ID_SELECT_SHELL, wxS("Select shell..."));
        menuBar->Append(terminalMenu, wxS("&Terminal"));

        auto* debugMenu = new wxMenu();
        debugMenu->Append(ID_START_DEBUG, wxS("Start debug adapter"));
        debugMenu->Append(ID_DEBUG_INITIALIZE, wxS("Initialize debug"));
        debugMenu->Append(ID_DEBUG_LAUNCH, wxS("Launch program"));
        debugMenu->Append(ID_DEBUG_CONTINUE, wxS("Continue"));
        debugMenu->Append(ID_DEBUG_PAUSE, wxS("Pause"));
        debugMenu->Append(ID_STOP_DEBUG, wxS("Stop debug"));
        menuBar->Append(debugMenu, wxS("De&bug"));

        auto* extensionMenu = new wxMenu();
        extensionMenu->Append(ID_START_HOST, wxS("Start Extension Host"));
        extensionMenu->Append(ID_LOAD_DEMO, wxS("Load demo extension"));
        extensionMenu->Append(ID_RUN_DEMO, wxS("Run hello.codium"));
        extensionMenu->Append(ID_INSTALL_VSIX, wxS("Install VSIX"));
        extensionMenu->Append(ID_LIST_EXTENSIONS, wxS("List installed extensions"));
        menuBar->Append(extensionMenu, wxS("E&xtensions"));

        SetMenuBar(menuBar);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenFile(); }, wxID_OPEN);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowCommandPalette(); }, ID_COMMAND_PALETTE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenWorkspace(); }, ID_OPEN_WORKSPACE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveFile(); }, wxID_SAVE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(true); }, wxID_EXIT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StartLanguageServer(); }, ID_START_CLANGD);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { InitializeLanguageServer(); }, ID_INITIALIZE_LSP);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RequestHover(); }, ID_HOVER);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RequestCompletion(); }, ID_COMPLETION);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StopLanguageServer(); }, ID_STOP_LSP);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { BuildProject(); }, ID_BUILD_PROJECT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RunSelectedTask(); }, ID_RUN_TASK);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StopTask(); }, ID_STOP_TASK);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StartTerminal(); }, ID_START_TERMINAL);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SendTerminalInput(); }, ID_SEND_TERMINAL);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StopTerminal(); }, ID_STOP_TERMINAL);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SelectShell(); }, ID_SELECT_SHELL);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StartDebugAdapter(); }, ID_START_DEBUG);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { InitializeDebug(); }, ID_DEBUG_INITIALIZE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { LaunchDebug(); }, ID_DEBUG_LAUNCH);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ContinueDebug(); }, ID_DEBUG_CONTINUE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { PauseDebug(); }, ID_DEBUG_PAUSE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StopDebug(); }, ID_STOP_DEBUG);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StartHost(); }, ID_START_HOST);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { LoadDemo(); }, ID_LOAD_DEMO);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ExecuteDemo(); }, ID_RUN_DEMO);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { InstallVsix(); }, ID_INSTALL_VSIX);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ListExtensions(); }, ID_LIST_EXTENSIONS);
    }

    template <typename Handler>
    void AddButton(wxSizer* sizer, const wxString& label, Handler&& handler, wxWindow* parent = nullptr)
    {
        wxWindow* owner = parent ? parent : sizer->GetContainingWindow();
        if (!owner) owner = this;
        auto* button = new wxButton(owner, wxID_ANY, label);
        button->Bind(wxEVT_BUTTON, std::forward<Handler>(handler));
        sizer->Add(button, 0, wxRIGHT, 6);
    }

    void ApplyAnsiCode(const wxString& code)
    {
        const wxArrayString parameters = wxSplit(code.empty() ? wxString(wxS("0")) : code, wxChar(';'));
        for (const auto& parameter : parameters) {
            const int value = wxAtoi(parameter);
            if (value == 0 || value == 39) ansiColour_ = wxColour(230, 230, 230);
            else if (value == 30) ansiColour_ = wxColour(0, 0, 0);
            else if (value == 31) ansiColour_ = wxColour(220, 70, 70);
            else if (value == 32) ansiColour_ = wxColour(80, 210, 100);
            else if (value == 33) ansiColour_ = wxColour(230, 210, 80);
            else if (value == 34) ansiColour_ = wxColour(90, 150, 240);
            else if (value == 35) ansiColour_ = wxColour(210, 100, 220);
            else if (value == 36) ansiColour_ = wxColour(70, 210, 210);
            else if (value == 37) ansiColour_ = wxColour(230, 230, 230);
            else if (value == 90) ansiColour_ = wxColour(130, 130, 130);
            else if (value == 91) ansiColour_ = wxColour(255, 100, 100);
            else if (value == 92) ansiColour_ = wxColour(120, 255, 140);
            else if (value == 93) ansiColour_ = wxColour(255, 240, 120);
            else if (value == 94) ansiColour_ = wxColour(130, 190, 255);
        }
    }

    void AppendTerminalOutput(const wxString& raw)
    {
        if (!terminalOutput_ || raw.empty()) return;
        wxString plain;
        const auto flush = [this, &plain]() {
            if (plain.empty()) return;
            terminalOutput_->BeginTextColour(ansiColour_);
            terminalOutput_->WriteText(plain);
            terminalOutput_->EndTextColour();
            plain.clear();
        };
        for (size_t i = 0; i < raw.length(); ++i) {
            if (raw[i] == wxChar(0x1b) && i + 1 < raw.length() && raw[i + 1] == wxChar('[')) {
                flush();
                size_t end = i + 2;
                while (end < raw.length() && !(raw[end] >= wxChar('@') && raw[end] <= wxChar('~'))) ++end;
                if (end < raw.length()) {
                    if (raw[end] == wxChar('m')) ApplyAnsiCode(raw.Mid(i + 2, end - i - 2));
                    i = end;
                    continue;
                }
            }
            if (raw[i] != wxChar('\r')) plain += raw[i];
        }
        flush();
        terminalOutput_->ShowPosition(terminalOutput_->GetLastPosition());
    }

    void RenderTerminalScreen()
    {
        if (!terminalOutput_) return;
        terminalOutput_->Freeze();
        terminalOutput_->Clear();
        for (int row = 0; row < terminalScreen_.Rows(); ++row) {
            int column = 0;
            while (column < terminalScreen_.Columns()) {
                const codium::TerminalCell& first = terminalScreen_.VisibleCellAt(column, row);
                if (first.continuation) {
                    ++column;
                    continue;
                }
                const bool firstCursor = terminalScreen_.ScrollOffset() == 0 && terminalScreen_.CursorVisible() &&
                    terminalScreen_.CursorColumn() == column && terminalScreen_.CursorRow() == row;
                const bool firstSelected = IsTerminalCellSelected(column, row);
                int foreground = first.foreground;
                int background = first.background;
                if (first.inverse) std::swap(foreground, background);
                if (firstCursor || firstSelected) foreground = 15;
                wxString run;
                run = first.text;
                if (run.empty()) run += first.character;
                ++column;
                while (column < terminalScreen_.Columns()) {
                    const codium::TerminalCell& cell = terminalScreen_.VisibleCellAt(column, row);
                    if (cell.continuation) {
                        ++column;
                        continue;
                    }
                    const bool cursor = terminalScreen_.ScrollOffset() == 0 && terminalScreen_.CursorVisible() &&
                        terminalScreen_.CursorColumn() == column && terminalScreen_.CursorRow() == row;
                    const bool selected = IsTerminalCellSelected(column, row);
                    int cellForeground = cell.foreground;
                    int cellBackground = cell.background;
                    if (cell.inverse) std::swap(cellForeground, cellBackground);
                    if (cursor || selected) cellForeground = 15;
                    if (cellForeground != foreground || cellBackground != background ||
                        cell.bold != first.bold || cell.underline != first.underline || cursor != firstCursor ||
                        selected != firstSelected || cell.hyperlink != first.hyperlink) break;
                    if (cell.text.empty()) run += cell.character;
                    else run += cell.text;
                    ++column;
                }
                wxRichTextAttr style;
                style.SetTextColour(firstSelected ? wxColour(255, 255, 255) :
                                    first.hyperlink.empty() ? codium::TerminalScreen::PaletteColor(foreground, first.bold)
                                                             : wxColour(80, 170, 255));
                style.SetBackgroundColour(firstSelected ? wxColour(40, 90, 170) : wxColour(20, 20, 20));
                style.SetFontWeight(first.bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL);
                style.SetFontUnderlined(first.underline || !first.hyperlink.empty());
                terminalOutput_->BeginStyle(style);
                terminalOutput_->WriteText(run);
                terminalOutput_->EndStyle();
            }
            if (row + 1 < terminalScreen_.Rows()) terminalOutput_->WriteText(wxS("\n"));
        }
        terminalOutput_->Thaw();
        terminalOutput_->ShowPosition(terminalOutput_->GetLastPosition());
    }

    void AppendLog(const wxString& line)
    {
        if (log_) {
            log_->AppendText(line + wxS("\n"));
        }
    }

    wxColour ProblemColour(codium::ProblemSeverity severity) const
    {
        if (severity == codium::ProblemSeverity::Error) return wxColour(210, 55, 55);
        if (severity == codium::ProblemSeverity::Warning) return wxColour(190, 130, 20);
        if (severity == codium::ProblemSeverity::Hint) return wxColour(100, 120, 180);
        return wxColour(70, 110, 180);
    }

    void ApplyInlineProblems()
    {
        if (!editor_) return;
        const long end = editor_->GetLastPosition();
        editor_->SetStyle(0, end, editor_->GetDefaultStyle());
        for (const auto& problem : problemStore_.Problems()) {
            if (problem.stale || problem.path.empty() || problem.path != document_.Path()) continue;
            const long start = editor_->XYToPosition(problem.column, problem.line);
            if (start == -1) continue;
            const long finish = std::min(end, std::max(start + 1, editor_->XYToPosition(problem.column + 1, problem.line)));
            wxTextAttr style;
            style.SetTextColour(ProblemColour(problem.severity));
            style.SetFontUnderlined(true);
            editor_->SetStyle(start, finish, style);
        }
    }

    void RefreshProblems()
    {
        if (!problems_) return;
        problems_->Freeze();
        problems_->Clear();
        problemLocations_.clear();
        problemPaths_.clear();
        const auto& values = problemStore_.Problems();
        for (const auto& problem : values) {
            const wxString stale = problem.stale ? wxS(" [stale]") : wxEmptyString;
            const wxString text = wxString::Format(wxS("%s%s  %s:%d:%d  %s"),
                                                   codium::ProblemParser::SeverityName(problem.severity), stale,
                                                   problem.path, problem.line + 1, problem.column + 1, problem.message);
            problems_->Append(text);
            problemLocations_.push_back({problem.line, problem.column});
            problemPaths_.push_back(problem.path);
        }
        problems_->Thaw();
        if (problemSummary_) {
            problemSummary_->SetLabel(wxString::Format(wxS("%zu problems  ·  %zu errors  ·  %zu warnings"),
                                                       values.size(), problemStore_.Count(codium::ProblemSeverity::Error),
                                                       problemStore_.Count(codium::ProblemSeverity::Warning)));
        }
        ApplyInlineProblems();
    }

    void AddLanguageProblem(const wxString& line)
    {
        codium::Problem problem;
        problem.source = wxS("LSP");
        problem.message = JsonStringField(line, wxS("message"));
        problem.path = JsonStringField(line, wxS("uri"));
        if (problem.path.StartsWith(wxS("file://"))) problem.path = problem.path.Mid(7);
        problem.path.Replace(wxS("%20"), wxS(" "));
        if (problem.path.empty()) problem.path = document_.Path();
        problem.line = std::max(0, JsonIntField(line, wxS("line"), 0));
        problem.column = std::max(0, JsonIntField(line, wxS("character"), 0));
        problem.endLine = problem.line;
        problem.endColumn = problem.column + 1;
        problem.code = JsonStringField(line, wxS("code"));
        problem.raw = line;
        const int severity = JsonIntField(line, wxS("severity"), 3);
        problem.severity = severity == 1 ? codium::ProblemSeverity::Error :
                           severity == 2 ? codium::ProblemSeverity::Warning :
                           severity == 4 ? codium::ProblemSeverity::Hint : codium::ProblemSeverity::Information;
        problemStore_.Add(problem);
        RefreshProblems();
    }

    void UpdateTitle()
    {
        const wxString name = document_.IsUntitled()
            ? (workspace_.IsOpen() ? workspace_.RootPath() : wxString(wxS("Untitled")))
            : wxFileName(document_.Path()).GetFullName();
        SetTitle(wxString::Format(wxS("%s%s — Codium::Blocks %s"),
                                  name, document_.IsDirty() ? wxS(" *") : wxEmptyString,
                                  CODIUM_BLOCKS_VERSION));
        if (GetStatusBar()) {
            SetStatusText(document_.IsUntitled() ? wxS("Untitled") : document_.Path(), 0);
            SetStatusText(workspace_.IsOpen() ? (workspace_.IsTrusted() ? wxS("Trusted workspace") : wxS("Untrusted workspace"))
                                              : wxS("No workspace"), 1);
            SetStatusText(wxString::Format(wxS("%s  Ln %d, Col %d"), languageId_,
                                           CurrentEditorLine() + 1, CurrentEditorCharacter() + 1), 2);
        }
    }

    void OpenFile()
    {
        wxFileDialog dialog(this, wxS("Open source file"), wxEmptyString, wxEmptyString,
                            wxS("All files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }

        OpenDocumentPath(dialog.GetPath());
    }

    void OpenWorkspace()
    {
        wxDirDialog dialog(this, wxS("Choose a workspace directory"), wxEmptyString,
                           wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) return;

        wxString error;
        if (!workspace_.Open(dialog.GetPath(), &error)) {
            AppendLog(wxS("Error: ") + error);
            return;
        }
        if (!workspace_.IsTrusted()) {
            const int answer = wxMessageBox(
                wxString::Format(wxS("Trust the workspace '%s'? Trust enables tasks, terminals, debug adapters, and extensions."), workspace_.RootPath()),
                wxS("Workspace trust"), wxYES_NO | wxICON_WARNING, this);
            if (answer == wxYES && workspace_.SetTrusted(true, &error)) AppendLog(wxS("Workspace trusted."));
            else AppendLog(wxS("Workspace is untrusted; execution features remain restricted."));
        }
        watchExpressions_ = codium::WatchStore::Load(workspace_.RootPath());
        RefreshWatchView();
        PopulateFileTree();
        RefreshNativeContributions();
        LoadProjectConfig();
        SetTitle(wxString::Format(wxS("%s — Codium::Blocks %s"), workspace_.RootPath(), CODIUM_BLOCKS_VERSION));
        UpdateTitle();
        AppendLog(wxS("Workspace opened: ") + workspace_.RootPath());
    }

    void RefreshNativeContributions()
    {
        if (treeViewsList_) {
            treeViewsList_->Clear();
            for (const auto& title : treeRegistry_.ViewTitles()) treeViewsList_->Append(title);
        }
        if (workspace_.IsOpen()) {
            wxArrayString labels;
            for (const auto& path : workspace_.Files()) labels.Add(workspace_.RelativePath(path));
            treeRegistry_.SetItems(wxS("workspace"), labels);
        }
        RefreshScm();
    }

    void RefreshScm()
    {
        if (!scm_) return;
        scm_->Clear();
        if (!workspace_.IsOpen()) {
            scm_->Append(wxS("Open a workspace to inspect source control."));
            return;
        }
        wxString error;
        if (!scmModel_.Refresh(workspace_.RootPath(), &error)) {
            scm_->Append(error);
            return;
        }
        for (const auto& resource : scmModel_.Resources()) scm_->Append(resource);
    }

    void LoadProjectConfig()
    {
        taskList_->Clear();
        wxString error;
        if (!projectConfig_.Load(workspace_.RootPath(), &error)) {
            AppendLog(wxS("Error: ") + error);
            return;
        }
        for (const auto& toolchain : projectConfig_.Toolchains()) {
            AppendLog(wxS("Detected toolchain: ") + toolchain);
        }
        for (const auto& task : projectConfig_.Tasks()) taskList_->Append(task.name);
        if (!projectConfig_.Tasks().empty()) taskList_->SetSelection(0);
    }

    void RunTask(const codium::ProjectTask& task)
    {
        problemStore_.Clear(task.name);
        if (buildOutput_) buildOutput_->AppendText(wxString::Format(wxS("\n=== %s ===\n"), task.name));
        wxString error;
        if (taskRunner_.Run(task, &error)) {
            AppendLog(wxString::Format(wxS("Started task: %s"), task.name));
            if (buildOutput_) buildOutput_->AppendText(wxString::Format(wxS("Started %s\n"), task.name));
            if (GetStatusBar()) SetStatusText(wxS("Building…"), 1);
        } else {
            AppendLog(wxS("Task error: ") + error);
            if (buildOutput_) buildOutput_->AppendText(wxS("Task error: ") + error + wxS("\n"));
        }
        RefreshProblems();
    }

    bool EnsureWorkspaceTrusted()
    {
        if (!workspace_.IsOpen() || workspace_.IsTrusted()) return true;
        AppendLog(wxS("Execution blocked: trust the current workspace first."));
        return false;
    }

    void BuildProject()
    {
        if (!EnsureWorkspaceTrusted()) return;
        if (!workspace_.IsOpen()) {
            AppendLog(wxS("Open a workspace before building."));
            return;
        }
        for (const auto& task : projectConfig_.Tasks()) {
            if (task.name.Contains(wxS("Build"))) {
                RunTask(task);
                return;
            }
        }
        AppendLog(wxS("No build task detected in this workspace."));
    }

    void RunSelectedTask()
    {
        if (!EnsureWorkspaceTrusted()) return;
        const int selection = taskList_ ? taskList_->GetSelection() : wxNOT_FOUND;
        if (selection == wxNOT_FOUND || selection >= static_cast<int>(projectConfig_.Tasks().size())) {
            AppendLog(wxS("Select a task before running it."));
            return;
        }
        RunTask(projectConfig_.Tasks()[selection]);
    }

    void StopTask()
    {
        if (taskRunner_.IsRunning()) {
            taskRunner_.Stop();
            AppendLog(wxS("Task stopped."));
        }
    }

    void OnTaskFinished(wxProcessEvent& event)
    {
        taskRunner_.HandleProcessExit(event.GetPid(), event.GetExitCode());
        AppendLog(wxString::Format(wxS("Task finished with exit code %d."), event.GetExitCode()));
        if (buildOutput_) buildOutput_->AppendText(wxString::Format(wxS("Finished with exit code %d.\n"), event.GetExitCode()));
        if (GetStatusBar()) SetStatusText(event.GetExitCode() == 0 ? wxS("Build succeeded") : wxS("Build failed"), 1);
        RefreshProblems();
    }

    wxString WorkspaceDirectory() const
    {
        return workspace_.IsOpen() ? workspace_.RootPath() : projectRoot_;
    }

    void SaveTerminalProfile()
    {
        terminalProfile_.shell = terminalShell_;
        terminalProfile_.columns = terminalColumns_;
        terminalProfile_.rows = terminalRows_;
        terminalProfile_.history.Clear();
        const size_t first = terminalHistory_.size() > 500 ? terminalHistory_.size() - 500 : 0;
        for (size_t index = first; index < terminalHistory_.size(); ++index) terminalProfile_.history.Add(terminalHistory_[index]);
        wxString error;
        if (!codium::TerminalProfileStore::Save(terminalProfile_, wxS("default"), &error)) AppendLog(error);
    }

    void StartTerminal()
    {
        if (!EnsureWorkspaceTrusted()) return;
        if (terminal_.IsRunning()) {
            AppendLog(wxS("Terminal is already running."));
            return;
        }
        wxArrayString arguments;
#if defined(__WXMSW__)
        arguments.Add(wxS("/Q"));
#else
        arguments.Add(wxS("-i"));
#endif
        wxString error;
        if (terminal_.Start(terminalShell_, arguments, WorkspaceDirectory(), &error)) {
            AppendLog(wxS("Native terminal started with backend: ") + terminal_.BackendName());
            if (GetStatusBar()) SetStatusText(wxS("Terminal running"), 1);
            terminalScreen_.Reset();
            if (terminalOutput_) {
                const wxSize size = terminalOutput_->GetClientSize();
                const int columns = std::max(20, size.GetWidth() / 8);
                const int rows = std::max(4, size.GetHeight() / 16);
                terminalColumns_ = columns;
                terminalRows_ = rows;
                terminal_.Resize(columns, rows);
                terminalScreen_.Resize(columns, rows);
                RenderTerminalScreen();
                terminalOutput_->SetFocus();
            }
        } else {
            AppendLog(wxS("Terminal error: ") + error);
        }
    }

    void SelectShell()
    {
        if (terminal_.IsRunning()) {
            AppendLog(wxS("Stop the terminal before changing its shell."));
            return;
        }
        wxArrayString choices;
#if defined(__WXMSW__)
        choices.Add(wxS("cmd.exe"));
        choices.Add(wxS("powershell.exe"));
        choices.Add(wxS("pwsh.exe"));
#else
        choices.Add(wxS("/bin/sh"));
        choices.Add(wxS("/bin/bash"));
        choices.Add(wxS("/bin/zsh"));
        choices.Add(wxS("/usr/bin/fish"));
#endif
        wxSingleChoiceDialog dialog(this, wxS("Select the shell for the next terminal session"),
                                    wxS("Terminal shell"), choices);
        if (dialog.ShowModal() == wxID_OK) {
            terminalShell_ = choices[dialog.GetSelection()];
            terminalProfile_.shell = terminalShell_;
            SaveTerminalProfile();
            AppendLog(wxS("Selected shell: ") + terminalShell_);
        }
    }

    void SendTerminalInput()
    {
        if (!terminal_.IsRunning()) {
            AppendLog(wxS("Start the terminal first."));
            return;
        }
        wxString input = terminalInput_ ? terminalInput_->GetValue() : wxString(wxEmptyString);
        if (!input.EndsWith(wxS("\n"))) input += wxS("\n");
        const wxString historyEntry = input.BeforeLast(wxChar('\n'));
        if (!historyEntry.empty()) {
            terminalHistory_.push_back(historyEntry);
            historyIndex_ = terminalHistory_.size();
        }
        if (terminal_.Write(input)) {
            if (terminalInput_) terminalInput_->Clear();
            SaveTerminalProfile();
        } else {
            AppendLog(wxS("Could not write to terminal."));
        }
    }

    wxPoint TerminalCellFromPosition(const wxPoint& position) const
    {
        wxClientDC dc(terminalOutput_);
        dc.SetFont(terminalOutput_->GetFont());
        int width = 8;
        int height = 16;
        dc.GetTextExtent(wxS("W"), &width, &height);
        return wxPoint(std::max(0, std::min(terminalScreen_.Columns() - 1, position.x / std::max(1, width))),
                       std::max(0, std::min(terminalScreen_.Rows() - 1, position.y / std::max(1, height))));
    }

    bool IsTerminalCellSelected(int column, int row) const
    {
        const int first = terminalSelectionAnchor_.y * terminalScreen_.Columns() + terminalSelectionAnchor_.x;
        const int last = terminalSelectionActive_.y * terminalScreen_.Columns() + terminalSelectionActive_.x;
        const int current = row * terminalScreen_.Columns() + column;
        return first != last && current >= std::min(first, last) && current <= std::max(first, last);
    }

    wxString SelectedTerminalText() const
    {
        if (!IsTerminalCellSelected(terminalSelectionAnchor_.x, terminalSelectionAnchor_.y)) return wxEmptyString;
        const int first = terminalSelectionAnchor_.y * terminalScreen_.Columns() + terminalSelectionAnchor_.x;
        const int last = terminalSelectionActive_.y * terminalScreen_.Columns() + terminalSelectionActive_.x;
        const int low = std::min(first, last);
        const int high = std::max(first, last);
        wxString text;
        int previousRow = -1;
        for (int index = low; index <= high; ++index) {
            const int row = index / terminalScreen_.Columns();
            const int column = index % terminalScreen_.Columns();
            const auto& cell = terminalScreen_.VisibleCellAt(column, row);
            if (cell.continuation) continue;
            if (previousRow >= 0 && row != previousRow) text += wxS("\n");
            if (cell.text.empty()) text += cell.character;
            else text += cell.text;
            previousRow = row;
        }
        return text;
    }

    void CopyTerminalSelection()
    {
        const wxString text = SelectedTerminalText();
        if (text.empty() || !wxTheClipboard || !wxTheClipboard->Open()) return;
        wxTheClipboard->SetData(new wxTextDataObject(text));
        wxTheClipboard->Close();
    }

    void SendTerminalMouse(int code, int column, int row, bool release)
    {
        if (!terminal_.IsRunning() || !terminalScreen_.MouseReporting()) return;
        column = std::max(0, std::min(terminalScreen_.Columns() - 1, column)) + 1;
        row = std::max(0, std::min(terminalScreen_.Rows() - 1, row)) + 1;
        wxString sequence;
        if (terminalScreen_.SgrMouse()) {
            sequence = wxString::Format(wxS("\x1b[<%d;%d;%d%c"), code, column, row, release ? wxChar('m') : wxChar('M'));
        } else {
            sequence += wxChar(0x1b);
            sequence += wxChar('[');
            sequence += wxChar('M');
            sequence += wxChar(32 + code);
            sequence += wxChar(32 + column);
            sequence += wxChar(32 + row);
        }
        terminal_.Write(sequence);
    }

    void SelectTerminalWord(const wxPoint& cell)
    {
        const auto isWord = [](wxChar character) {
            return (character >= wxChar('a') && character <= wxChar('z')) ||
                   (character >= wxChar('A') && character <= wxChar('Z')) ||
                   (character >= wxChar('0') && character <= wxChar('9')) || character == wxChar('_');
        };
        const auto& current = terminalScreen_.VisibleCellAt(cell.x, cell.y);
        if (!isWord(current.character)) {
            terminalSelectionAnchor_ = cell;
            terminalSelectionActive_ = cell;
            return;
        }
        int left = cell.x;
        int right = cell.x;
        while (left > 0 && isWord(terminalScreen_.VisibleCellAt(left - 1, cell.y).character)) --left;
        while (right + 1 < terminalScreen_.Columns() && isWord(terminalScreen_.VisibleCellAt(right + 1, cell.y).character)) ++right;
        terminalSelectionAnchor_ = wxPoint(left, cell.y);
        terminalSelectionActive_ = wxPoint(right, cell.y);
    }

    void SelectTerminalLine(const wxPoint& cell)
    {
        terminalSelectionAnchor_ = wxPoint(0, cell.y);
        terminalSelectionActive_ = wxPoint(terminalScreen_.Columns() - 1, cell.y);
    }

    void BeginTerminalSelection(wxMouseEvent& event)
    {
        const wxPoint cell = TerminalCellFromPosition(event.GetPosition());
        if (terminalScreen_.MouseReporting()) {
            const int code = event.RightDown() ? 2 : event.MiddleDown() ? 1 : 0;
            SendTerminalMouse(code, cell.x, cell.y, false);
            return;
        }
        if (event.GetClickCount() >= 3) {
            SelectTerminalLine(cell);
            terminalSelecting_ = false;
            RenderTerminalScreen();
            return;
        }
        if (event.GetClickCount() == 2) {
            SelectTerminalWord(cell);
            terminalSelecting_ = false;
            RenderTerminalScreen();
            return;
        }
        terminalSelectionAnchor_ = cell;
        terminalSelectionActive_ = cell;
        terminalSelecting_ = true;
        terminalOutput_->SetFocus();
        terminalOutput_->CaptureMouse();
        RenderTerminalScreen();
    }

    void UpdateTerminalSelection(wxMouseEvent& event)
    {
        if (terminalScreen_.MouseReporting()) {
            if (event.Moving() || event.Dragging()) {
                const wxPoint cell = TerminalCellFromPosition(event.GetPosition());
                SendTerminalMouse(32, cell.x, cell.y, false);
            }
            return;
        }
        if (!terminalSelecting_ || !event.Dragging()) return;
        terminalSelectionActive_ = TerminalCellFromPosition(event.GetPosition());
        RenderTerminalScreen();
    }

    void EndTerminalSelection(wxMouseEvent& event)
    {
        const wxPoint cell = TerminalCellFromPosition(event.GetPosition());
        if (terminalScreen_.MouseReporting()) {
            const int code = event.RightUp() ? 2 : event.MiddleUp() ? 1 : 0;
            SendTerminalMouse(code, cell.x, cell.y, true);
            return;
        }
        const bool clickedSingleCell = terminalSelecting_ && terminalSelectionAnchor_ == cell;
        if (terminalSelecting_) terminalSelectionActive_ = cell;
        terminalSelecting_ = false;
        if (terminalOutput_->HasCapture()) terminalOutput_->ReleaseMouse();
        if (clickedSingleCell) {
            const wxString& link = terminalScreen_.VisibleCellAt(cell.x, cell.y).hyperlink;
            if (!link.empty()) wxLaunchDefaultBrowser(link);
        }
        RenderTerminalScreen();
    }

    void HandleTerminalWheel(wxMouseEvent& event)
    {
        const wxPoint cell = TerminalCellFromPosition(event.GetPosition());
        const int clicks = std::max(1, std::abs(event.GetWheelRotation()) / std::max(1, event.GetWheelDelta()));
        if (terminalScreen_.MouseReporting()) {
            SendTerminalMouse(event.GetWheelRotation() > 0 ? 64 : 65, cell.x, cell.y, false);
        } else if (event.GetWheelRotation() > 0) {
            terminalScreen_.ScrollBack(clicks);
            RenderTerminalScreen();
        } else {
            terminalScreen_.ScrollForward(clicks);
            RenderTerminalScreen();
        }
    }

    void HandleTerminalKey(wxKeyEvent& event)
    {
        if (!terminal_.IsRunning()) {
            event.Skip();
            return;
        }

        if (event.ControlDown() && (event.GetUnicodeKey() == wxChar('c') || event.GetUnicodeKey() == wxChar('C')) &&
            !SelectedTerminalText().empty()) {
            CopyTerminalSelection();
            return;
        }
        if (event.ControlDown() && (event.GetUnicodeKey() == wxChar('v') || event.GetUnicodeKey() == wxChar('V')) &&
            wxTheClipboard && wxTheClipboard->Open()) {
            wxTextDataObject data;
            if (wxTheClipboard->IsSupported(wxDF_TEXT) && wxTheClipboard->GetData(data)) {
                wxString paste = data.GetText();
                if (terminalScreen_.BracketedPaste()) paste = wxString::FromUTF8("\x1b[200~") + paste + wxString::FromUTF8("\x1b[201~");
                terminal_.Write(paste);
            }
            wxTheClipboard->Close();
            return;
        }

        wxString bytes;
        const int key = event.GetKeyCode();
        switch (key) {
        case WXK_RETURN: case WXK_NUMPAD_ENTER: bytes = wxS("\r"); break;
        case WXK_BACK: bytes = wxString::FromUTF8("\x7f"); break;
        case WXK_TAB: bytes = wxS("\t"); break;
        case WXK_ESCAPE: bytes = wxString::FromUTF8("\x1b"); break;
        case WXK_UP: bytes = wxString::FromUTF8("\x1b[A"); break;
        case WXK_DOWN: bytes = wxString::FromUTF8("\x1b[B"); break;
        case WXK_RIGHT: bytes = wxString::FromUTF8("\x1b[C"); break;
        case WXK_LEFT: bytes = wxString::FromUTF8("\x1b[D"); break;
        case WXK_HOME: bytes = wxString::FromUTF8("\x1b[H"); break;
        case WXK_END: bytes = wxString::FromUTF8("\x1b[F"); break;
        case WXK_DELETE: bytes = wxString::FromUTF8("\x1b[3~"); break;
        case WXK_INSERT: bytes = wxString::FromUTF8("\x1b[2~"); break;
        case WXK_PAGEUP: bytes = wxString::FromUTF8("\x1b[5~"); break;
        case WXK_PAGEDOWN: bytes = wxString::FromUTF8("\x1b[6~"); break;
        default: {
            const wxChar unicode = static_cast<wxChar>(event.GetUnicodeKey());
            if (unicode == wxChar(WXK_NONE) || unicode == wxChar(0)) {
                event.Skip();
                return;
            }
            if (event.ControlDown() && unicode >= wxChar('a') && unicode <= wxChar('z')) {
                bytes += static_cast<wxChar>(unicode - wxChar('a') + 1);
            } else if (event.ControlDown() && unicode >= wxChar('A') && unicode <= wxChar('Z')) {
                bytes += static_cast<wxChar>(unicode - wxChar('A') + 1);
            } else {
                if (event.AltDown()) bytes += wxString::FromUTF8("\x1b");
                bytes += unicode;
            }
            break;
        }
        }
        if (!bytes.empty()) terminal_.Write(bytes);
    }

    void StopTerminal()
    {
        if (terminal_.IsRunning()) {
            terminal_.Stop();
            AppendLog(wxS("Terminal stopped."));
        }
    }

    void OnTerminalFinished(wxProcessEvent& event)
    {
        terminal_.HandleProcessExit(event.GetPid(), event.GetExitCode());
        AppendLog(wxString::Format(wxS("Terminal finished with exit code %d."), event.GetExitCode()));
        if (GetStatusBar()) SetStatusText(event.GetExitCode() == 0 ? wxS("Terminal exited") : wxS("Terminal failed"), 1);
    }

    void StartDebugAdapter()
    {
        if (!EnsureWorkspaceTrusted()) return;
        if (dap_.IsRunning()) {
            AppendLog(wxS("Debug adapter is already running."));
            return;
        }
        wxTextEntryDialog dialog(this, wxS("Debug adapter executable (for example codelldb or OpenDebugAD7)"),
                                 wxS("Start debug adapter"), wxS("codelldb"));
        if (dialog.ShowModal() != wxID_OK || dialog.GetValue().empty()) return;
        wxArrayString arguments;
        wxString error;
        if (dap_.Start(dialog.GetValue(), arguments, WorkspaceDirectory(), &error)) {
            AppendLog(wxS("Debug adapter started."));
        } else {
            AppendLog(wxS("Debug adapter error: ") + error);
        }
    }

    void InitializeDebug()
    {
        if (!dap_.IsRunning()) {
            AppendLog(wxS("Start a debug adapter first."));
            return;
        }
        if (dap_.SendRequest(wxS("initialize"),
                             wxS("{\"clientID\":\"codium-blocks\",\"adapterID\":\"codium-blocks\",\"linesStartAt1\":true,\"columnsStartAt1\":true}"))) {
            AppendLog(wxS("DAP initialize request sent."));
        }
    }

    void LaunchDebug()
    {
        if (!dap_.IsRunning()) {
            AppendLog(wxS("Start a debug adapter first."));
            return;
        }
        wxFileDialog dialog(this, wxS("Choose a program to debug"), wxEmptyString, wxEmptyString,
                            wxS("Executable files (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) return;
        const wxString program = dialog.GetPath();
        wxString launchArguments = wxString::Format(wxS("{\"program\":\"%s\",\"cwd\":\"%s\",\"sourceFileMap\":%s}"),
                                                    JsonEscape(program), JsonEscape(WorkspaceDirectory()), SourceMapArguments());
        if (dap_.SendRequest(wxS("launch"), launchArguments)) {
            AppendLog(wxS("DAP launch request sent."));
            if (!document_.IsUntitled()) {
                const auto found = breakpointLines_.find(document_.Path());
                wxArrayInt lines;
                if (found != breakpointLines_.end()) for (const int line : found->second) lines.Add(line);
                dap_.SetBreakpoints(document_.Path(), lines);
            }
        }
    }

    void RefreshBreakpointView()
    {
        if (!breakpoints_) return;
        breakpoints_->Clear();
        const auto found = breakpointLines_.find(document_.Path());
        if (found == breakpointLines_.end()) return;
        for (const int line : found->second) breakpoints_->Append(wxString::Format(wxS("%s:%d"), document_.Path(), line));
    }

    void ToggleBreakpoint()
    {
        if (document_.IsUntitled()) {
            AppendLog(wxS("Open a source file before toggling a breakpoint."));
            return;
        }
        const int line = CurrentEditorLine() + 1;
        auto& lines = breakpointLines_[document_.Path()];
        const auto found = std::find(lines.begin(), lines.end(), line);
        if (found == lines.end()) lines.push_back(line);
        else lines.erase(found);
        std::sort(lines.begin(), lines.end());
        RefreshBreakpointView();
        if (dap_.IsRunning()) {
            wxArrayInt dapLines;
            for (const int value : lines) dapLines.Add(value);
            if (dap_.SetBreakpoints(document_.Path(), dapLines)) AppendLog(wxS("DAP setBreakpoints request sent."));
        }
    }

    void RequestDebugThreads()
    {
        if (dap_.RequestThreads()) AppendLog(wxS("DAP threads request sent."));
        else AppendLog(wxS("Start and initialize a debug adapter first."));
    }

    void RequestStackTrace()
    {
        if (dap_.RequestStackTrace(debugThreadId_)) AppendLog(wxS("DAP stackTrace request sent."));
        else AppendLog(wxS("Start and initialize a debug adapter first."));
    }

    void RequestDebugScopes()
    {
        if (dap_.RequestScopes(debugFrameId_)) AppendLog(wxS("DAP scopes request sent."));
    }

    void RequestDebugVariables()
    {
        if (dap_.RequestVariables(debugVariablesReference_)) AppendLog(wxS("DAP variables request sent."));
    }

    void EvaluateDebugExpression()
    {
        wxTextEntryDialog dialog(this, wxS("Expression to evaluate"), wxS("Debug evaluate"), wxEmptyString);
        if (dialog.ShowModal() != wxID_OK || dialog.GetValue().empty()) return;
        if (dap_.Evaluate(dialog.GetValue(), debugFrameId_)) AppendLog(wxS("DAP evaluate request sent."));
    }

    void RefreshWatchView()
    {
        if (!watches_) return;
        watches_->Clear();
        for (const auto& expression : watchExpressions_) watches_->Append(expression);
    }

    void AddWatch()
    {
        wxTextEntryDialog dialog(this, wxS("Expression to watch"), wxS("Add watch"), wxEmptyString);
        if (dialog.ShowModal() != wxID_OK || dialog.GetValue().empty()) return;
        if (watchExpressions_.Index(dialog.GetValue()) == wxNOT_FOUND) watchExpressions_.Add(dialog.GetValue());
        RefreshWatchView();
        wxString error;
        if (workspace_.IsOpen() && !codium::WatchStore::Save(workspace_.RootPath(), watchExpressions_, &error)) AppendLog(error);
        if (dap_.IsRunning()) {
            if (dap_.Evaluate(dialog.GetValue(), debugFrameId_)) AppendLog(wxS("DAP watch evaluate request sent."));
        }
    }

    void ConfigureSourceMap()
    {
        wxTextEntryDialog remote(this, wxS("Remote source root"), wxS("Source mapping"), wxEmptyString);
        if (remote.ShowModal() != wxID_OK || remote.GetValue().empty()) return;
        wxTextEntryDialog local(this, wxS("Local source root"), wxS("Source mapping"), WorkspaceDirectory());
        if (local.ShowModal() != wxID_OK || local.GetValue().empty()) return;
        sourceMapper_.Add(remote.GetValue(), local.GetValue());
        AppendLog(wxS("Source mapping added: ") + remote.GetValue() + wxS(" -> ") + local.GetValue());
    }

    wxString SourceMapArguments() const
    {
        return sourceMapper_.ToJson();
    }

    void ContinueDebug()
    {
        if (dap_.SendRequest(wxS("continue"), wxS("{\"threadId\":1}"))) AppendLog(wxS("DAP continue request sent."));
    }

    void PauseDebug()
    {
        if (dap_.SendRequest(wxS("pause"), wxS("{\"threadId\":1}"))) AppendLog(wxS("DAP pause request sent."));
    }

    void StopDebug()
    {
        if (dap_.IsRunning()) {
            dap_.Stop();
            AppendLog(wxS("Debug adapter stopped."));
        }
    }

    void OnDebugFinished(wxProcessEvent& event)
    {
        dap_.HandleProcessExit(event.GetPid(), event.GetExitCode());
        AppendLog(wxString::Format(wxS("Debug adapter finished with exit code %d."), event.GetExitCode()));
    }

    void ShowCommandPalette()
    {
        const wxArrayString commands = {
            wxS("Open workspace"), wxS("Open file"), wxS("Save file"),
            wxS("Start clangd"), wxS("Initialize language server"),
            wxS("Request hover"), wxS("Request completion"),
            wxS("Build project"), wxS("Run selected task"), wxS("Stop task"),
            wxS("Start terminal"), wxS("Send terminal input"), wxS("Stop terminal"), wxS("Select shell"),
            wxS("Start debug adapter"), wxS("Initialize debug"), wxS("Continue debug"),
            wxS("Launch debug program"), wxS("Pause debug"), wxS("Stop debug"),
            wxS("Start Extension Host"), wxS("Load demo extension"),
            wxS("Run hello.codium"), wxS("Stop language server"),
            wxS("Install VSIX"), wxS("List installed extensions")
        };
        wxSingleChoiceDialog dialog(this, wxS("Select a command"), wxS("Command Palette"), commands);
        if (dialog.ShowModal() != wxID_OK) return;
        switch (dialog.GetSelection()) {
        case 0: OpenWorkspace(); break;
        case 1: OpenFile(); break;
        case 2: SaveFile(); break;
        case 3: StartLanguageServer(); break;
        case 4: InitializeLanguageServer(); break;
        case 5: RequestHover(); break;
        case 6: RequestCompletion(); break;
        case 7: BuildProject(); break;
        case 8: RunSelectedTask(); break;
        case 9: StopTask(); break;
        case 10: StartTerminal(); break;
        case 11: SendTerminalInput(); break;
        case 12: StopTerminal(); break;
        case 13: SelectShell(); break;
        case 14: StartDebugAdapter(); break;
        case 15: InitializeDebug(); break;
        case 16: LaunchDebug(); break;
        case 17: ContinueDebug(); break;
        case 18: PauseDebug(); break;
        case 19: StopDebug(); break;
        case 20: StartHost(); break;
        case 21: LoadDemo(); break;
        case 22: ExecuteDemo(); break;
        case 23: StopLanguageServer(); break;
        case 24: InstallVsix(); break;
        case 25: ListExtensions(); break;
        default: break;
        }
    }

    void PopulateFileTree()
    {
        fileTree_->DeleteAllItems();
        if (!workspace_.IsOpen()) return;
        const wxTreeItemId root = fileTree_->AddRoot(wxFileName(workspace_.RootPath()).GetFullName());
        for (const auto& absolute : workspace_.Files()) {
            wxTreeItemId parent = root;
            wxString relative = workspace_.RelativePath(absolute);
            relative.Replace(wxS("\\"), wxS("/"));
            wxArrayString parts = wxSplit(relative, wxChar('/'));
            wxString accumulated;
            for (size_t i = 0; i < parts.GetCount(); ++i) {
                if (!accumulated.empty()) accumulated += wxFILE_SEP_PATH;
                accumulated += parts[i];
                wxTreeItemId child;
                wxTreeItemIdValue cookie;
                bool found = false;
                if (fileTree_->ItemHasChildren(parent)) {
                    child = fileTree_->GetFirstChild(parent, cookie);
                    while (child.IsOk()) {
                        if (fileTree_->GetItemText(child) == parts[i]) { found = true; break; }
                        child = fileTree_->GetNextChild(parent, cookie);
                    }
                }
                if (!found) child = fileTree_->AppendItem(parent, parts[i]);
                parent = child;
            }
            fileTree_->SetItemData(parent, new FileTreeData(absolute));
        }
        fileTree_->Expand(root);
    }

    void OpenTreeItem(wxTreeEvent& event)
    {
        auto* data = dynamic_cast<FileTreeData*>(fileTree_->GetItemData(event.GetItem()));
        if (data) OpenDocumentPath(data->Path());
    }

    void OpenDocumentPath(const wxString& path)
    {
        for (size_t i = 0; i < tabPaths_.size(); ++i) {
            if (tabPaths_[i] == path && !path.empty()) {
                notebook_->SetSelection(static_cast<int>(i));
                SwitchToTab(i);
                return;
            }
        }

        codium::Document loaded;
        wxString error;
        if (!loaded.Load(path, &error)) {
            AppendLog(wxS("Error: ") + error);
            return;
        }

        auto* page = new wxTextCtrl(notebook_, wxID_ANY, loaded.Text(), wxDefaultPosition, wxDefaultSize,
                                    wxTE_MULTILINE | wxTE_RICH2 | wxHSCROLL);
        page->SetFont(wxFont(11, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        page->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
            OnEditorChanged(static_cast<wxTextCtrl*>(event.GetEventObject()));
        });
        documents_[path] = loaded;
        tabPaths_.push_back(path);
        notebook_->AddPage(page, wxFileName(path).GetFullName(), true);
        SwitchToTab(tabPaths_.size() - 1);
        const wxString customEditor = customEditors_.Resolve(path);
        if (!customEditor.empty()) AppendLog(wxS("Custom editor selected: ") + customEditor);
        AppendLog(wxS("Opened: ") + path);
        NotifyLanguageDocumentOpened();
    }

    void OnEditorChanged(wxTextCtrl* source)
    {
        if (loadingDocument_ || !source) return;
        const int pageIndex = notebook_->FindPage(source);
        if (pageIndex == wxNOT_FOUND || pageIndex >= static_cast<int>(tabPaths_.size())) return;
        const wxString path = tabPaths_[pageIndex];
        if (path.empty()) {
            document_.SetText(source->GetValue());
        } else {
            documents_[path].SetText(source->GetValue());
            if (source == editor_) document_ = documents_[path];
        }
        if (source == editor_) {
            ++documentVersion_;
            NotifyLanguageDocumentChanged();
            UpdateTitle();
        }
    }

    void SwitchToTab(size_t index)
    {
        if (index >= tabPaths_.size()) return;
        if (editor_) {
            const int previous = notebook_->FindPage(editor_);
            if (previous != wxNOT_FOUND && previous < static_cast<int>(tabPaths_.size()) && !tabPaths_[previous].empty()) {
                documents_[tabPaths_[previous]].SetText(editor_->GetValue());
            }
        }
        editor_ = dynamic_cast<wxTextCtrl*>(notebook_->GetPage(static_cast<int>(index)));
        if (!tabPaths_[index].empty()) {
            document_ = documents_[tabPaths_[index]];
        } else {
            document_ = codium::Document();
            documentVersion_ = 1;
        }
        languageId_ = document_.IsUntitled() ? wxS("plaintext") : LanguageIdForPath(document_.Path());
        UpdateTitle();
        ApplyInlineProblems();
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
            languageId_ = LanguageIdForPath(document_.Path());
            const int pageIndex = notebook_->GetSelection();
            if (pageIndex != wxNOT_FOUND && pageIndex < static_cast<int>(tabPaths_.size())) {
                tabPaths_[pageIndex] = document_.Path();
                documents_[document_.Path()] = document_;
                notebook_->SetPageText(pageIndex, wxFileName(document_.Path()).GetFullName());
            }
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
        if (!EnsureWorkspaceTrusted()) return;
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
        host_.OpenLanguageDocument(DocumentUri(), languageId_, documentVersion_, document_.Text());
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
        if (!EnsureWorkspaceTrusted()) return;
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

    void SearchOpenVsx()
    {
        if (!EnsureWorkspaceTrusted()) return;
        wxTextEntryDialog dialog(this, wxS("Search Open VSX"), wxS("Extension registry"), wxEmptyString);
        if (dialog.ShowModal() != wxID_OK || dialog.GetValue().empty()) return;
        wxString catalog;
        wxString error;
        if (extensionRegistry_.SearchOpenVsx(wxS("https://open-vsx.org"), dialog.GetValue(), &catalog, &error)) {
            AppendLog(wxString::Format(wxS("Open VSX catalog received (%lu bytes); cache updated."),
                                       static_cast<unsigned long>(catalog.length())));
        } else {
            AppendLog(wxS("Open VSX error: ") + error);
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

    void ShowDiagnostics(const wxString& line)
    {
        problemStore_.Clear(wxS("LSP"));
        AddLanguageProblem(line);
    }

    void GoToProblem(int index)
    {
        if (index < 0 || index >= static_cast<int>(problemLocations_.size())) return;
        if (bottomWorkbench_) bottomWorkbench_->SetSelection(0);
        if (index < static_cast<int>(problemPaths_.size()) && !problemPaths_[static_cast<size_t>(index)].empty() &&
            problemPaths_[static_cast<size_t>(index)] != document_.Path() &&
            wxFileExists(problemPaths_[static_cast<size_t>(index)])) {
            OpenDocumentPath(problemPaths_[static_cast<size_t>(index)]);
        }
        if (!editor_) return;
        const auto [line, character] = problemLocations_[static_cast<size_t>(index)];
        const long position = editor_->XYToPosition(character, line);
        if (position != -1) {
            editor_->SetInsertionPoint(position);
            editor_->ShowPosition(position);
            editor_->SetFocus();
        }
    }

    void ShowDebugMessage(const wxString& line)
    {
        if (debugConsole_) debugConsole_->AppendText(line + wxS("\n"));
        const wxString command = JsonStringField(line, wxS("command"));
        const wxString event = JsonStringField(line, wxS("event"));
        if (event == wxS("stopped")) {
            debugThreadId_ = JsonIntField(line, wxS("threadId"), debugThreadId_);
            RequestDebugThreads();
            return;
        }
        if (command == wxS("initialize")) {
            if (debugCapabilities_) {
                debugCapabilities_->Clear();
                const wxArrayString capabilities = {
                    wxS("supportsConfigurationDoneRequest"), wxS("supportsTerminateRequest"),
                    wxS("supportsSetVariable"), wxS("supportsEvaluateForHovers"),
                    wxS("supportsRestartRequest"), wxS("supportsStepBack")};
                for (const auto& capability : capabilities) {
                    const wxString marker = wxString::Format(wxS("\"%s\":true"), capability);
                    debugCapabilities_->Append(capability + (line.Find(marker) != wxNOT_FOUND ? wxS(": yes") : wxS(": no/unknown")));
                }
            }
            dap_.ConfigurationDone();
        } else if (command == wxS("threads")) {
            if (debugThreads_) {
                debugThreads_->Clear();
                const wxArrayString names = JsonStringFields(line, wxS("name"));
                for (const auto& name : names) debugThreads_->Append(name);
                if (names.IsEmpty()) debugThreads_->Append(line);
            }
            debugThreadId_ = JsonIntField(line, wxS("id"), debugThreadId_);
            RequestStackTrace();
        } else if (command == wxS("stackTrace")) {
            debugFrameLocations_.clear();
            const wxArrayString names = JsonStringFields(line, wxS("name"));
            const std::vector<int> ids = JsonIntFields(line, wxS("id"));
            const std::vector<int> lines = JsonIntFields(line, wxS("line"));
            const std::vector<int> columns = JsonIntFields(line, wxS("column"));
            const wxArrayString paths = JsonStringFields(line, wxS("path"));
            for (size_t index = 0; index < names.size(); ++index) {
                DebugFrameLocation frame;
                frame.id = index < ids.size() ? ids[index] : debugFrameId_;
                frame.line = index < lines.size() ? lines[index] : 1;
                frame.character = index < columns.size() ? columns[index] : 1;
                frame.path = index < paths.size() ? paths[index] : wxString(wxEmptyString);
                debugFrameLocations_.push_back(frame);
            }
            if (callStack_) {
                callStack_->Clear();
                for (const auto& name : names) callStack_->Append(name);
                if (names.IsEmpty()) callStack_->Append(line);
            }
            debugFrameId_ = JsonIntField(line, wxS("id"), debugFrameId_);
            RequestDebugScopes();
        } else if (command == wxS("scopes")) {
            debugVariablesReference_ = JsonIntField(line, wxS("variablesReference"), debugVariablesReference_);
            RequestDebugVariables();
        } else if (command == wxS("variables") || command == wxS("evaluate")) {
            if (variables_) {
                variables_->Append(line);
                const wxArrayString names = JsonStringFields(line, wxS("name"));
                for (const auto& name : names) variables_->Append(name);
            }
        } else if (command == wxS("setBreakpoints")) {
            AppendLog(wxS("DAP breakpoints response received."));
        }
    }

    void GoToStackFrame(int index)
    {
        if (index < 0 || index >= static_cast<int>(debugFrameLocations_.size())) return;
        const DebugFrameLocation& frame = debugFrameLocations_[static_cast<size_t>(index)];
        debugFrameId_ = frame.id;
        const wxString mappedPath = sourceMapper_.Map(frame.path);
        if (!mappedPath.empty() && wxFileExists(mappedPath)) OpenDocumentPath(mappedPath);
        if (editor_) {
            const long position = editor_->XYToPosition(std::max(0, frame.character), std::max(0, frame.line - 1));
            if (position != -1) {
                editor_->SetInsertionPoint(position);
                editor_->ShowPosition(position);
                editor_->SetFocus();
            }
        }
        RequestDebugScopes();
    }

    void ShowLanguageResult(const wxString& line)
    {
        if (!hover_) {
            return;
        }
        const wxString method = JsonStringField(line, wxS("method"));
        const wxString value = JsonStringField(line, wxS("value"));
        const wxString label = JsonStringField(line, wxS("label"));
        if (method == wxS("textDocument/hover")) {
            hover_->SetValue(value.empty() ? line : value);
        } else if (method == wxS("textDocument/completion")) {
            if (completion_) {
                completion_->Clear();
                completion_->Append(label.empty() ? line : label);
            }
            hover_->SetValue(label.empty() ? line : wxString::Format(wxS("Completion: %s"), label));
        } else if (method == wxS("initialize")) {
            hover_->SetValue(wxS("Language server initialized."));
        }
    }

    void OnTimer(wxTimerEvent&)
    {
        bool problemsChanged = false;
        for (const auto& line : taskRunner_.Poll()) {
            AppendLog(wxS("task> ") + line);
            if (buildOutput_) buildOutput_->AppendText(line + wxS("\n"));
            problemStore_.AddCompilerLine(line, taskRunner_.CurrentTask().name, WorkspaceDirectory());
            problemsChanged = true;
        }
        const wxString terminalRaw = terminal_.PollRaw();
        if (terminalScreen_.Feed(terminalRaw)) RenderTerminalScreen();
        if (!terminalRaw.empty()) {
            const wxArrayString terminalLines = wxSplit(terminalRaw, wxChar('\n'));
            for (const auto& line : terminalLines) {
                problemStore_.AddCompilerLine(line, wxS("Terminal"), WorkspaceDirectory());
                problemsChanged = true;
            }
        }
        for (const auto& message : dap_.Poll()) {
            AppendLog(wxS("dap> ") + message);
            ShowDebugMessage(message);
        }
        for (const auto& line : host_.Poll()) {
            AppendLog(wxS("host> ") + line);
            if (line.Find(wxS("\"event\":\"languageServerMessage\"")) != wxNOT_FOUND) {
                AppendLog(wxS("LSP message received."));
            }
            if (line.Find(wxS("\"event\":\"diagnostics\"")) != wxNOT_FOUND) {
                AppendLog(wxS("LSP diagnostics updated."));
                ShowDiagnostics(line);
                problemsChanged = true;
            }
            if (line.Find(wxS("\"event\":\"languageServerResult\"")) != wxNOT_FOUND) {
                ShowLanguageResult(line);
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
        if (problemsChanged) RefreshProblems();
    }

    void OnClose(wxCloseEvent& event)
    {
        SaveTerminalProfile();
        taskRunner_.Stop();
        terminal_.Stop();
        dap_.Stop();
        host_.Stop();
        event.Skip();
    }

    wxString projectRoot_;
    codium::Workspace workspace_;
    codium::ExtensionHostClient host_;
    codium::ProjectConfig projectConfig_;
    codium::TaskRunner taskRunner_;
    codium::TerminalSession terminal_;
    codium::TerminalScreen terminalScreen_;
    codium::DapClient dap_;
    wxString terminalShell_;
    codium::TerminalProfile terminalProfile_;
    int terminalColumns_ = 120;
    int terminalRows_ = 32;
    codium::VsixManager extensions_;
    codium::ExtensionRegistry extensionRegistry_;
    codium::TreeViewRegistry treeRegistry_;
    codium::ScmModel scmModel_;
    codium::CustomEditorRegistry customEditors_;
    codium::ProblemStore problemStore_;
    codium::Document document_;
    std::map<wxString, codium::Document> documents_;
    std::vector<wxString> tabPaths_;
    wxTreeCtrl* fileTree_ = nullptr;
    wxListBox* treeViewsList_ = nullptr;
    wxListBox* scm_ = nullptr;
    wxListBox* customEditorsView_ = nullptr;
    wxNotebook* notebook_ = nullptr;
    wxNotebook* bottomWorkbench_ = nullptr;
    wxListBox* taskList_ = nullptr;
    wxListBox* breakpoints_ = nullptr;
    wxListBox* debugThreads_ = nullptr;
    wxListBox* callStack_ = nullptr;
    wxListBox* variables_ = nullptr;
    wxListBox* watches_ = nullptr;
    wxListBox* debugCapabilities_ = nullptr;
    wxTextCtrl* terminalInput_ = nullptr;
    wxRichTextCtrl* terminalOutput_ = nullptr;
    wxBoxSizer* extensionControls_ = nullptr;
    wxTextCtrl* editor_ = nullptr;
    wxListBox* diagnostics_ = nullptr;
    wxListBox* problems_ = nullptr;
    wxStaticText* problemSummary_ = nullptr;
    wxTextCtrl* buildOutput_ = nullptr;
    wxTextCtrl* debugConsole_ = nullptr;
    wxTextCtrl* hover_ = nullptr;
    wxListBox* completion_ = nullptr;
    wxTextCtrl* log_ = nullptr;
    wxTimer timer_;
    bool loadingDocument_ = false;
    int documentVersion_ = 1;
    bool languageServerInitialized_ = false;
    wxString languageId_ = wxS("plaintext");
    wxColour ansiColour_ = wxColour(230, 230, 230);
    std::vector<wxString> terminalHistory_;
    size_t historyIndex_ = 0;
    wxPoint terminalSelectionAnchor_;
    wxPoint terminalSelectionActive_;
    bool terminalSelecting_ = false;
    std::map<wxString, std::vector<int>> breakpointLines_;
    std::vector<std::pair<int, int>> problemLocations_;
    std::vector<wxString> problemPaths_;
    int debugThreadId_ = 1;
    int debugFrameId_ = 1;
    int debugVariablesReference_ = 1;
    codium::SourceMapper sourceMapper_;
    wxArrayString watchExpressions_;
    std::vector<DebugFrameLocation> debugFrameLocations_;

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
