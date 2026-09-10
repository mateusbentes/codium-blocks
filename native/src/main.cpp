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
#include "codium/codeblocks_bridge.hpp"
#include "codium/codeblocks_adapter_client.hpp"
#include "codium/problem_model.hpp"
#include "codium/vsix_manager.hpp"
#include "codium/workspace.hpp"

#include <wx/button.h>
#include <wx/choicdlg.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dcclient.h>
#include <wx/dcbuffer.h>
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/choice.h>
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
    ID_DISCOVER_CODEBLOCKS,
    ID_START_CODEBLOCKS_ADAPTER,
    ID_STOP_CODEBLOCKS_ADAPTER,
    ID_OPEN_WORKSPACE,
    ID_COMMAND_PALETTE,
    ID_BUILD_PROJECT,
    ID_RUN_TASK,
    ID_STOP_TASK,
    ID_RERUN_BUILD,
    ID_NEXT_PROBLEM,
    ID_PREVIOUS_PROBLEM,
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

wxString FindCodeBlocksDataDirectory(const codium::CodeBlocksBridge& bridge)
{
    const wxString roots[] = {
        bridge.Root(),
        bridge.Root() + wxS("/share/codeblocks"),
        bridge.Root() + wxS("/../share/codeblocks")
    };
    for (const auto& root : roots) {
        if (wxFileExists(root + wxS("/resources.zip"))) return wxFileName(root).GetFullPath();
    }
    return wxEmptyString;
}

wxString FindCodeBlocksCompilerPlugin(const codium::CodeBlocksBridge& bridge)
{
    const wxString names[] = {
        wxS("libcompiler.so"), wxS("libcompiler.dylib"), wxS("compiler.dylib"),
        wxS("compiler.dll"), wxS("compiler.so")
    };
    for (const auto& directory : bridge.PluginDirectories()) {
        for (const auto& name : names) {
            const wxString candidate = directory + wxFILE_SEP_PATH + name;
            if (wxFileExists(candidate)) return wxFileName(candidate).GetFullPath();
        }
    }
    return wxEmptyString;
}

wxString FindCodeBlocksDebuggerPlugin(const codium::CodeBlocksBridge& bridge)
{
    const wxString names[] = {
        wxS("libdebugger.so"), wxS("libdebugger.dylib"), wxS("debugger.dylib"),
        wxS("debugger.dll"), wxS("debugger.so")
    };
    for (const auto& directory : bridge.PluginDirectories()) {
        for (const auto& name : names) {
            const wxString candidate = directory + wxFILE_SEP_PATH + name;
            if (wxFileExists(candidate)) return wxFileName(candidate).GetFullPath();
        }
    }
    return wxEmptyString;
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

struct GutterMarker final {
    int line = 0;
    codium::ProblemSeverity severity = codium::ProblemSeverity::Information;
};

class ProblemGutter final : public wxPanel {
public:
    explicit ProblemGutter(wxWindow* parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(58, -1), wxBORDER_NONE)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        Bind(wxEVT_PAINT, &ProblemGutter::OnPaint, this);
        Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    }

    void SetMarkers(const std::vector<GutterMarker>& markers)
    {
        markers_ = markers;
        Refresh();
    }

    void SetFirstLine(int firstLine)
    {
        firstLine_ = std::max(0, firstLine);
        Refresh();
    }

private:
    wxColour MarkerColour(codium::ProblemSeverity severity) const
    {
        if (severity == codium::ProblemSeverity::Error) return wxColour(215, 65, 65);
        if (severity == codium::ProblemSeverity::Warning) return wxColour(205, 145, 30);
        if (severity == codium::ProblemSeverity::Hint) return wxColour(100, 125, 180);
        return wxColour(70, 120, 190);
    }

    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetBackgroundColour()));
        dc.Clear();
        dc.SetFont(GetFont());
        const int lineHeight = std::max(1, dc.GetCharHeight() + 2);
        const int visibleLines = GetClientSize().GetHeight() / lineHeight + 1;
        for (int line = 0; line < visibleLines; ++line) {
            const int documentLine = firstLine_ + line;
            const wxString number = wxString::Format(wxS("%d"), documentLine + 1);
            int width = 0;
            dc.GetTextExtent(number, &width, nullptr);
            dc.SetTextForeground(wxColour(125, 125, 125));
            dc.DrawText(number, std::max(20, GetClientSize().GetWidth() - width - 6), line * lineHeight);
            for (const auto& marker : markers_) {
                if (marker.line != documentLine) continue;
                dc.SetBrush(wxBrush(MarkerColour(marker.severity)));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawCircle(8, line * lineHeight + lineHeight / 2, 4);
                break;
            }
        }
    }

    std::vector<GutterMarker> markers_;
    int firstLine_ = 0;
};

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
          codeBlocksAdapter_(this),
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
            wxS("Codium::Blocks — native classic IDE — no Electron"));
        title->SetFont(title->GetFont().Bold());
        root->Add(title, 0, wxALL | wxEXPAND, 8);

        auto* mainSplitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                                  wxSP_LIVE_UPDATE | wxSP_3D);
        auto* navigatorPanel = new wxPanel(mainSplitter);
        auto* centerPanel = new wxPanel(mainSplitter);
        mainSplitter->SetMinimumPaneSize(190);
        mainSplitter->SplitVertically(navigatorPanel, centerPanel, 270);
        root->Add(mainSplitter, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

        auto* navigatorRoot = new wxBoxSizer(wxVERTICAL);
        auto* navigatorTitle = new wxStaticText(navigatorPanel, wxID_ANY, wxS("PROJECT NAVIGATOR"));
        navigatorTitle->SetFont(navigatorTitle->GetFont().Bold());
        navigatorRoot->Add(navigatorTitle, 0, wxALL | wxEXPAND, 6);
        auto* workspaceControls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(workspaceControls, wxS("Open"), [this](wxCommandEvent&) { OpenWorkspace(); }, navigatorPanel);
        AddButton(workspaceControls, wxS("SCM"), [this](wxCommandEvent&) { RefreshScm(); }, navigatorPanel);
        navigatorRoot->Add(workspaceControls, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);
        fileTree_ = new wxTreeCtrl(navigatorPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxTR_DEFAULT_STYLE | wxTR_SINGLE);
        fileTree_->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& event) { OpenTreeItem(event); });
        navigatorRoot->Add(fileTree_, 1, wxLEFT | wxRIGHT | wxEXPAND, 6);
        navigatorRoot->Add(new wxStaticText(navigatorPanel, wxID_ANY, wxS("Tree Views")), 0, wxLEFT | wxRIGHT | wxTOP, 6);
        treeViewsList_ = new wxListBox(navigatorPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 50));
        navigatorRoot->Add(treeViewsList_, 0, wxLEFT | wxRIGHT | wxEXPAND, 6);
        navigatorRoot->Add(new wxStaticText(navigatorPanel, wxID_ANY, wxS("Source Control")), 0, wxLEFT | wxRIGHT | wxTOP, 6);
        scm_ = new wxListBox(navigatorPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 60));
        navigatorRoot->Add(scm_, 0, wxLEFT | wxRIGHT | wxEXPAND, 6);
        navigatorRoot->Add(new wxStaticText(navigatorPanel, wxID_ANY, wxS("Custom editors")), 0, wxLEFT | wxRIGHT | wxTOP, 6);
        customEditorsView_ = new wxListBox(navigatorPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 48));
        for (const auto& entry : customEditors_.Entries()) customEditorsView_->Append(entry);
        navigatorRoot->Add(customEditorsView_, 0, wxLEFT | wxRIGHT | wxEXPAND, 6);
        navigatorRoot->Add(new wxStaticText(navigatorPanel, wxID_ANY, wxS("Tasks")), 0, wxLEFT | wxRIGHT | wxTOP, 6);
        taskList_ = new wxListBox(navigatorPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 70));
        navigatorRoot->Add(taskList_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);
        navigatorPanel->SetSizer(navigatorRoot);

        auto* centerRoot = new wxBoxSizer(wxVERTICAL);
        auto* schemeBar = new wxBoxSizer(wxHORIZONTAL);
        schemeBar->Add(new wxStaticText(centerPanel, wxID_ANY, wxS("Scheme")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        schemeChoice_ = new wxChoice(centerPanel, wxID_ANY);
        schemeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SelectScheme(); });
        schemeBar->Add(schemeChoice_, 0, wxRIGHT, 8);
        schemeBar->Add(new wxStaticText(centerPanel, wxID_ANY, wxS("Target")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        targetChoice_ = new wxChoice(centerPanel, wxID_ANY);
        targetChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateSchemeStatus(); UpdateTitle(); });
        schemeBar->Add(targetChoice_, 0, wxRIGHT, 8);
        schemeBar->Add(new wxStaticText(centerPanel, wxID_ANY, wxS("Toolchain")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        toolchainChoice_ = new wxChoice(centerPanel, wxID_ANY);
        toolchainChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SelectToolchain(); });
        schemeBar->Add(toolchainChoice_, 0, wxRIGHT, 8);
        schemeStatus_ = new wxStaticText(centerPanel, wxID_ANY, wxS("No project scheme"));
        schemeBar->Add(schemeStatus_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
        centerRoot->Add(schemeBar, 0, wxBOTTOM | wxEXPAND, 6);

        auto* commandBar = new wxBoxSizer(wxHORIZONTAL);
        AddButton(commandBar, wxS("Open file"), [this](wxCommandEvent&) { OpenFile(); }, centerPanel);
        AddButton(commandBar, wxS("Save"), [this](wxCommandEvent&) { SaveFile(); }, centerPanel);
        AddButton(commandBar, wxS("Build"), [this](wxCommandEvent&) { BuildProject(); }, centerPanel);
        AddButton(commandBar, wxS("Run task"), [this](wxCommandEvent&) { RunSelectedTask(); }, centerPanel);
        AddButton(commandBar, wxS("Palette"), [this](wxCommandEvent&) { ShowCommandPalette(); }, centerPanel);
        centerRoot->Add(commandBar, 0, wxBOTTOM | wxEXPAND, 6);

        notebook_ = new wxNotebook(centerPanel, wxID_ANY);
        ProblemGutter* initialGutter = nullptr;
        wxPanel* initialPage = CreateEditorPage(notebook_, wxEmptyString, &editor_, &initialGutter);
        notebook_->AddPage(initialPage, wxS("Untitled"), true);
        tabPaths_.push_back(wxEmptyString);
        editorPages_.push_back(editor_);
        editorGutters_.push_back(initialGutter);
        notebook_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& event) {
            SwitchToTab(static_cast<size_t>(event.GetSelection()));
            event.Skip();
        });
        centerRoot->Add(notebook_, 1, wxEXPAND);

        bottomWorkbench_ = new wxNotebook(centerPanel, wxID_ANY);
        bottomWorkbench_->SetMinSize(wxSize(-1, 245));
        auto* problemsPage = new wxPanel(bottomWorkbench_);
        auto* problemsRoot = new wxBoxSizer(wxVERTICAL);
        problemSummary_ = new wxStaticText(problemsPage, wxID_ANY, wxS("No problems"));
        problemsRoot->Add(problemSummary_, 0, wxALL | wxEXPAND, 6);
        auto* problemFilters = new wxBoxSizer(wxHORIZONTAL);
        problemFilters->Add(new wxStaticText(problemsPage, wxID_ANY, wxS("Severity")),
                            0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        problemSeverityChoice_ = new wxChoice(problemsPage, wxID_ANY);
        problemSeverityChoice_->Append(wxS("All"));
        problemSeverityChoice_->Append(wxS("Errors"));
        problemSeverityChoice_->Append(wxS("Warnings"));
        problemSeverityChoice_->Append(wxS("Information"));
        problemSeverityChoice_->Append(wxS("Hints"));
        problemSeverityChoice_->SetSelection(0);
        problemSeverityChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { RefreshProblems(); });
        problemFilters->Add(problemSeverityChoice_, 0, wxRIGHT, 8);
        problemFilters->Add(new wxStaticText(problemsPage, wxID_ANY, wxS("Source")),
                            0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        problemSourceChoice_ = new wxChoice(problemsPage, wxID_ANY);
        problemSourceChoice_->Append(wxS("All sources"));
        problemSourceChoice_->SetSelection(0);
        problemSourceChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { RefreshProblems(); });
        problemFilters->Add(problemSourceChoice_, 0, wxRIGHT, 8);
        AddButton(problemFilters, wxS("Previous"), [this](wxCommandEvent&) { SelectAdjacentProblem(-1); }, problemsPage);
        AddButton(problemFilters, wxS("Next"), [this](wxCommandEvent&) { SelectAdjacentProblem(1); }, problemsPage);
        AddButton(problemFilters, wxS("Rerun Build"), [this](wxCommandEvent&) { RunLastBuild(); }, problemsPage);
        problemsRoot->Add(problemFilters, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);
        problems_ = new wxListBox(problemsPage, wxID_ANY);
        problems_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) { GoToProblem(event.GetSelection()); });
        diagnostics_ = problems_;
        problemsRoot->Add(problems_, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);
        problemsPage->SetSizer(problemsRoot);
        bottomWorkbench_->AddPage(problemsPage, wxS("Problems"), true);

        auto* buildPage = new wxPanel(bottomWorkbench_);
        auto* buildRoot = new wxBoxSizer(wxVERTICAL);
        buildOutput_ = new wxTextCtrl(buildPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);
        buildRoot->Add(buildOutput_, 1, wxALL | wxEXPAND, 6);
        buildPage->SetSizer(buildRoot);
        bottomWorkbench_->AddPage(buildPage, wxS("Build"));

        auto* terminalPage = new wxPanel(bottomWorkbench_);
        auto* terminalRoot = new wxBoxSizer(wxVERTICAL);
        auto* terminalControls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(terminalControls, wxS("Start"), [this](wxCommandEvent&) { StartTerminal(); }, terminalPage);
        AddButton(terminalControls, wxS("Send input"), [this](wxCommandEvent&) { SendTerminalInput(); }, terminalPage);
        AddButton(terminalControls, wxS("Stop"), [this](wxCommandEvent&) { StopTerminal(); }, terminalPage);
        AddButton(terminalControls, wxS("Shell"), [this](wxCommandEvent&) { SelectShell(); }, terminalPage);
        terminalRoot->Add(terminalControls, 0, wxBOTTOM | wxEXPAND, 4);
        terminalInput_ = new wxTextCtrl(terminalPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 28));
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
            } else event.Skip();
        });
        terminalRoot->Add(terminalInput_, 0, wxBOTTOM | wxEXPAND, 4);
        terminalOutput_ = new wxRichTextCtrl(terminalPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                             wxRE_MULTILINE | wxRE_READONLY | wxHSCROLL);
        terminalOutput_->SetBackgroundColour(wxColour(20, 20, 20));
        terminalOutput_->BeginTextColour(wxColour(230, 230, 230));
        terminalOutput_->EndTextColour();
        terminalOutput_->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            if (terminal_.IsRunning()) {
                const wxSize size = terminalOutput_->GetClientSize();
                terminalColumns_ = std::max(20, size.GetWidth() / 8);
                terminalRows_ = std::max(4, size.GetHeight() / 16);
                terminal_.Resize(terminalColumns_, terminalRows_);
                terminalScreen_.Resize(terminalColumns_, terminalRows_);
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
        terminalRoot->Add(terminalOutput_, 1, wxEXPAND);
        terminalPage->SetSizer(terminalRoot);
        bottomWorkbench_->AddPage(terminalPage, wxS("Terminal"));

        auto* debugPage = new wxPanel(bottomWorkbench_);
        auto* debugRoot = new wxBoxSizer(wxVERTICAL);
        auto* debugControls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(debugControls, wxS("Start adapter"), [this](wxCommandEvent&) { StartDebugAdapter(); }, debugPage);
        AddButton(debugControls, wxS("Initialize"), [this](wxCommandEvent&) { InitializeDebug(); }, debugPage);
        AddButton(debugControls, wxS("Launch"), [this](wxCommandEvent&) { LaunchDebug(); }, debugPage);
        AddButton(debugControls, wxS("Breakpoint"), [this](wxCommandEvent&) { ToggleBreakpoint(); }, debugPage);
        AddButton(debugControls, wxS("Continue"), [this](wxCommandEvent&) { ContinueDebug(); }, debugPage);
        AddButton(debugControls, wxS("Pause"), [this](wxCommandEvent&) { PauseDebug(); }, debugPage);
        AddButton(debugControls, wxS("Stop"), [this](wxCommandEvent&) { StopDebug(); }, debugPage);
        AddButton(debugControls, wxS("Watch"), [this](wxCommandEvent&) { AddWatch(); }, debugPage);
        AddButton(debugControls, wxS("Source map"), [this](wxCommandEvent&) { ConfigureSourceMap(); }, debugPage);
        debugRoot->Add(debugControls, 0, wxBOTTOM | wxEXPAND, 4);
        auto* debugColumns = new wxBoxSizer(wxHORIZONTAL);
        auto* debugLeft = new wxBoxSizer(wxVERTICAL);
        breakpoints_ = new wxListBox(debugPage, wxID_ANY);
        debugLeft->Add(new wxStaticText(debugPage, wxID_ANY, wxS("Breakpoints")), 0, wxBOTTOM, 2);
        debugLeft->Add(breakpoints_, 1, wxEXPAND);
        debugThreads_ = new wxListBox(debugPage, wxID_ANY);
        debugLeft->Add(new wxStaticText(debugPage, wxID_ANY, wxS("Threads")), 0, wxTOP | wxBOTTOM, 2);
        debugLeft->Add(debugThreads_, 1, wxEXPAND);
        debugColumns->Add(debugLeft, 1, wxRIGHT | wxEXPAND, 6);
        auto* debugMiddle = new wxBoxSizer(wxVERTICAL);
        callStack_ = new wxListBox(debugPage, wxID_ANY);
        callStack_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) { GoToStackFrame(event.GetSelection()); });
        debugMiddle->Add(new wxStaticText(debugPage, wxID_ANY, wxS("Call stack")), 0, wxBOTTOM, 2);
        debugMiddle->Add(callStack_, 1, wxEXPAND);
        watches_ = new wxListBox(debugPage, wxID_ANY);
        debugMiddle->Add(new wxStaticText(debugPage, wxID_ANY, wxS("Watches")), 0, wxTOP | wxBOTTOM, 2);
        debugMiddle->Add(watches_, 1, wxEXPAND);
        debugColumns->Add(debugMiddle, 1, wxRIGHT | wxEXPAND, 6);
        auto* debugRight = new wxBoxSizer(wxVERTICAL);
        variables_ = new wxListBox(debugPage, wxID_ANY);
        debugRight->Add(new wxStaticText(debugPage, wxID_ANY, wxS("Variables")), 0, wxBOTTOM, 2);
        debugRight->Add(variables_, 1, wxEXPAND);
        debugCapabilities_ = new wxListBox(debugPage, wxID_ANY);
        debugRight->Add(new wxStaticText(debugPage, wxID_ANY, wxS("Capabilities")), 0, wxTOP | wxBOTTOM, 2);
        debugRight->Add(debugCapabilities_, 1, wxEXPAND);
        debugColumns->Add(debugRight, 1, wxEXPAND);
        debugRoot->Add(debugColumns, 1, wxEXPAND);
        debugConsole_ = new wxTextCtrl(debugPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                       wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
        debugRoot->Add(debugConsole_, 0, wxTOP | wxEXPAND, 4);
        debugPage->SetSizer(debugRoot);
        bottomWorkbench_->AddPage(debugPage, wxS("Debug"));

        auto* outputPage = new wxPanel(bottomWorkbench_);
        auto* outputRoot = new wxBoxSizer(wxVERTICAL);
        auto* fileControls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(fileControls, wxS("Start clangd"), [this](wxCommandEvent&) { StartLanguageServer(); }, outputPage);
        AddButton(fileControls, wxS("Initialize LSP"), [this](wxCommandEvent&) { InitializeLanguageServer(); }, outputPage);
        AddButton(fileControls, wxS("Hover"), [this](wxCommandEvent&) { RequestHover(); }, outputPage);
        AddButton(fileControls, wxS("Completion"), [this](wxCommandEvent&) { RequestCompletion(); }, outputPage);
        AddButton(fileControls, wxS("Stop LSP"), [this](wxCommandEvent&) { StopLanguageServer(); }, outputPage);
        outputRoot->Add(fileControls, 0, wxBOTTOM | wxEXPAND, 4);
        auto* extensionControls = new wxBoxSizer(wxHORIZONTAL);
        extensionControls_ = extensionControls;
        AddButton(extensionControls, wxS("Start Host"), [this](wxCommandEvent&) { StartHost(); }, outputPage);
        AddButton(extensionControls, wxS("Load demo"), [this](wxCommandEvent&) { LoadDemo(); }, outputPage);
        AddButton(extensionControls, wxS("Run demo"), [this](wxCommandEvent&) { ExecuteDemo(); }, outputPage);
        AddButton(extensionControls, wxS("Install VSIX"), [this](wxCommandEvent&) { InstallVsix(); }, outputPage);
        AddButton(extensionControls, wxS("Search Open VSX"), [this](wxCommandEvent&) { SearchOpenVsx(); }, outputPage);
        AddButton(extensionControls, wxS("Discover Code::Blocks"), [this](wxCommandEvent&) { DiscoverCodeBlocks(); }, outputPage);
        AddButton(extensionControls, wxS("Start CB adapter"), [this](wxCommandEvent&) { StartCodeBlocksAdapter(); }, outputPage);
        AddButton(extensionControls, wxS("Stop CB adapter"), [this](wxCommandEvent&) { StopCodeBlocksAdapter(); }, outputPage);
        outputRoot->Add(extensionControls, 0, wxBOTTOM | wxEXPAND, 4);
        hover_ = new wxTextCtrl(outputPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 60),
                                wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
        outputRoot->Add(hover_, 0, wxBOTTOM | wxEXPAND, 4);
        completion_ = new wxListBox(outputPage, wxID_ANY, wxDefaultPosition, wxSize(-1, 55));
        outputRoot->Add(completion_, 0, wxBOTTOM | wxEXPAND, 4);
        log_ = new wxTextCtrl(outputPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);
        outputRoot->Add(log_, 1, wxEXPAND);
        outputPage->SetSizer(outputRoot);
        bottomWorkbench_->AddPage(outputPage, wxS("Output"));

        centerRoot->Add(bottomWorkbench_, 0, wxEXPAND | wxTOP, 6);
        centerPanel->SetSizer(centerRoot);
        SetSizer(root);
        CreateStatusBar(3);
        SetStatusText(wxS("Ready"), 0);
        SetStatusText(wxS("No workspace"), 1);
        SetStatusText(wxS("UTF-8"), 2);
        Centre();

        Bind(wxEVT_END_PROCESS, [this](wxProcessEvent& event) { OnTaskFinished(event); }, ID_TASK_PROCESS);
        Bind(wxEVT_END_PROCESS, [this](wxProcessEvent& event) { OnTerminalFinished(event); }, ID_TERMINAL_PROCESS);
        Bind(wxEVT_END_PROCESS, [this](wxProcessEvent& event) { OnDebugFinished(event); }, ID_DAP_PROCESS);

        timer_.Start(50);
        AppendLog(wxS("Ready. The native core does not load Electron."));
        AppendLog(wxS("Open a file or start the host to use extensions and language tooling."));
        wxString codeBlocksError;
        if (codeBlocksBridge_.Discover(&codeBlocksError)) {
            AppendLog(wxString::Format(wxS("Code::Blocks SDK detected at %s (%lu plugin candidate(s))."),
                                       codeBlocksBridge_.Root(),
                                       static_cast<unsigned long>(codeBlocksBridge_.Plugins().size())));
        } else {
            AppendLog(wxS("Code::Blocks SDK discovery: ") + codeBlocksError);
        }
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
        buildMenu->Append(ID_RERUN_BUILD, wxS("Rerun last build\tCtrl+Shift+B"));
        buildMenu->Append(ID_RUN_TASK, wxS("Run selected task"));
        buildMenu->Append(ID_STOP_TASK, wxS("Stop task"));
        buildMenu->AppendSeparator();
        buildMenu->Append(ID_PREVIOUS_PROBLEM, wxS("Previous problem\tShift+F8"));
        buildMenu->Append(ID_NEXT_PROBLEM, wxS("Next problem\tF8"));
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
        extensionMenu->Append(ID_DISCOVER_CODEBLOCKS, wxS("Discover Code::Blocks SDK"));
        extensionMenu->Append(ID_START_CODEBLOCKS_ADAPTER, wxS("Start Code::Blocks adapter"));
        extensionMenu->Append(ID_STOP_CODEBLOCKS_ADAPTER, wxS("Stop Code::Blocks adapter"));
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
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RunLastBuild(); }, ID_RERUN_BUILD);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RunSelectedTask(); }, ID_RUN_TASK);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StopTask(); }, ID_STOP_TASK);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SelectAdjacentProblem(-1); }, ID_PREVIOUS_PROBLEM);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SelectAdjacentProblem(1); }, ID_NEXT_PROBLEM);
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
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { DiscoverCodeBlocks(); }, ID_DISCOVER_CODEBLOCKS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StartCodeBlocksAdapter(); }, ID_START_CODEBLOCKS_ADAPTER);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StopCodeBlocksAdapter(); }, ID_STOP_CODEBLOCKS_ADAPTER);
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

    wxPanel* CreateEditorPage(wxWindow* parent, const wxString& text,
                              wxTextCtrl** editorOut, ProblemGutter** gutterOut)
    {
        auto* page = new wxPanel(parent);
        auto* layout = new wxBoxSizer(wxHORIZONTAL);
        auto* gutter = new ProblemGutter(page);
        auto* editor = new wxTextCtrl(page, wxID_ANY, text, wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE | wxTE_RICH2 | wxHSCROLL);
        editor->SetFont(wxFont(11, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        editor->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
            OnEditorChanged(static_cast<wxTextCtrl*>(event.GetEventObject()));
        });
        const auto onScroll = [gutter, editor](wxScrollWinEvent& event) {
            gutter->SetFirstLine(editor->GetScrollPos(wxVERTICAL));
            event.Skip();
        };
        editor->Bind(wxEVT_SCROLLWIN_TOP, onScroll);
        editor->Bind(wxEVT_SCROLLWIN_BOTTOM, onScroll);
        editor->Bind(wxEVT_SCROLLWIN_LINEUP, onScroll);
        editor->Bind(wxEVT_SCROLLWIN_LINEDOWN, onScroll);
        editor->Bind(wxEVT_SCROLLWIN_PAGEUP, onScroll);
        editor->Bind(wxEVT_SCROLLWIN_PAGEDOWN, onScroll);
        editor->Bind(wxEVT_SCROLLWIN_THUMBTRACK, onScroll);
        editor->Bind(wxEVT_SCROLLWIN_THUMBRELEASE, onScroll);
        layout->Add(gutter, 0, wxEXPAND);
        layout->Add(editor, 1, wxEXPAND);
        page->SetSizer(layout);
        if (editorOut) *editorOut = editor;
        if (gutterOut) *gutterOut = gutter;
        return page;
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

    bool ProblemMatchesFilter(const codium::Problem& problem) const
    {
        const int severity = problemSeverityChoice_ ? problemSeverityChoice_->GetSelection() : 0;
        if (severity == 1 && problem.severity != codium::ProblemSeverity::Error) return false;
        if (severity == 2 && problem.severity != codium::ProblemSeverity::Warning) return false;
        if (severity == 3 && problem.severity != codium::ProblemSeverity::Information) return false;
        if (severity == 4 && problem.severity != codium::ProblemSeverity::Hint) return false;
        if (problemSourceChoice_ && problemSourceChoice_->GetSelection() > 0 &&
            problemSourceChoice_->GetStringSelection() != problem.source) return false;
        return true;
    }

    void RefreshProblems()
    {
        if (!problems_) return;
        const wxString selectedSource = problemSourceChoice_ ? problemSourceChoice_->GetStringSelection()
                                                              : wxString(wxEmptyString);
        if (problemSourceChoice_) {
            std::set<wxString> sources;
            for (const auto& problem : problemStore_.Problems()) {
                if (!problem.source.empty()) sources.insert(problem.source);
            }
            problemSourceChoice_->Freeze();
            problemSourceChoice_->Clear();
            problemSourceChoice_->Append(wxS("All sources"));
            for (const auto& source : sources) problemSourceChoice_->Append(source);
            if (!selectedSource.empty() && problemSourceChoice_->FindString(selectedSource) != wxNOT_FOUND) {
                problemSourceChoice_->SetStringSelection(selectedSource);
            } else {
                problemSourceChoice_->SetSelection(0);
            }
            problemSourceChoice_->Thaw();
        }
        problems_->Freeze();
        problems_->Clear();
        problemLocations_.clear();
        problemPaths_.clear();
        problemIndices_.clear();
        const auto& values = problemStore_.Problems();
        for (size_t index = 0; index < values.size(); ++index) {
            const auto& problem = values[index];
            if (!ProblemMatchesFilter(problem)) continue;
            const wxString stale = problem.stale ? wxS(" [stale]") : wxEmptyString;
            const wxString text = wxString::Format(wxS("%s%s  %s:%d:%d  %s"),
                                                   codium::ProblemParser::SeverityName(problem.severity), stale,
                                                   problem.path, problem.line + 1, problem.column + 1, problem.message);
            problems_->Append(text);
            problemLocations_.push_back({problem.line, problem.column});
            problemPaths_.push_back(problem.path);
            problemIndices_.push_back(index);
        }
        problems_->Thaw();
        if (problemSummary_) {
            problemSummary_->SetLabel(wxString::Format(wxS("%zu shown / %zu problems  ·  %zu errors  ·  %zu warnings"),
                                                       problemIndices_.size(), values.size(),
                                                       problemStore_.Count(codium::ProblemSeverity::Error),
                                                       problemStore_.Count(codium::ProblemSeverity::Warning)));
        }
        RefreshGutters();
        ApplyInlineProblems();
    }

    void RefreshGutters()
    {
        for (size_t tab = 0; tab < editorGutters_.size() && tab < tabPaths_.size(); ++tab) {
            if (!editorGutters_[tab]) continue;
            std::vector<GutterMarker> markers;
            for (const auto& problem : problemStore_.Problems()) {
                if (!problem.stale && !problem.path.empty() && problem.path == tabPaths_[tab]) {
                    markers.push_back(GutterMarker{problem.line, problem.severity});
                }
            }
            editorGutters_[tab]->SetMarkers(markers);
        }
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
            wxString workspaceStatus = workspace_.IsOpen() ? (workspace_.IsTrusted() ? wxS("Trusted workspace") : wxS("Untrusted workspace"))
                                                             : wxS("No workspace");
            if (selectedSchemeIndex_ >= 0 && selectedSchemeIndex_ < static_cast<int>(projectConfig_.Schemes().size())) {
                workspaceStatus += wxS(" · ") + projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)].name;
            }
            SetStatusText(workspaceStatus, 1);
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
        PopulateSchemeBar();
    }

    void PopulateSchemeBar()
    {
        if (!schemeChoice_ || !targetChoice_ || !toolchainChoice_) return;
        schemeChoice_->Clear();
        targetChoice_->Clear();
        toolchainChoice_->Clear();
        selectedSchemeIndex_ = wxNOT_FOUND;
        for (const auto& scheme : projectConfig_.Schemes()) {
            schemeChoice_->Append(scheme.name);
            if (targetChoice_->FindString(scheme.target) == wxNOT_FOUND) targetChoice_->Append(scheme.target);
            if (toolchainChoice_->FindString(scheme.toolchain) == wxNOT_FOUND) toolchainChoice_->Append(scheme.toolchain);
        }
        if (schemeChoice_->GetCount() == 0) {
            if (schemeStatus_) schemeStatus_->SetLabel(wxS("Open a project with a detected toolchain"));
            return;
        }
        selectedSchemeIndex_ = 0;
        schemeChoice_->SetSelection(0);
        const auto& selected = projectConfig_.Schemes().front();
        targetChoice_->SetStringSelection(selected.target);
        toolchainChoice_->SetStringSelection(selected.toolchain);
        UpdateSchemeStatus();
        UpdateTitle();
    }

    void SelectScheme()
    {
        if (!schemeChoice_ || schemeChoice_->GetSelection() == wxNOT_FOUND) return;
        selectedSchemeIndex_ = schemeChoice_->GetSelection();
        if (selectedSchemeIndex_ >= static_cast<int>(projectConfig_.Schemes().size())) return;
        const auto& selected = projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)];
        targetChoice_->SetStringSelection(selected.target);
        toolchainChoice_->SetStringSelection(selected.toolchain);
        UpdateSchemeStatus();
        UpdateTitle();
        AppendLog(wxString::Format(wxS("Selected scheme: %s (%s, target %s)"), selected.name,
                                   selected.configuration, selected.target));
    }

    void SelectToolchain()
    {
        if (!toolchainChoice_ || toolchainChoice_->GetSelection() == wxNOT_FOUND) return;
        const wxString toolchain = toolchainChoice_->GetStringSelection();
        const wxString configuration = selectedSchemeIndex_ >= 0 &&
            selectedSchemeIndex_ < static_cast<int>(projectConfig_.Schemes().size())
                ? projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)].configuration
                : wxString(wxS("Debug"));
        for (size_t index = 0; index < projectConfig_.Schemes().size(); ++index) {
            const auto& scheme = projectConfig_.Schemes()[index];
            if (scheme.toolchain == toolchain && scheme.configuration == configuration) {
                selectedSchemeIndex_ = static_cast<int>(index);
                schemeChoice_->SetSelection(selectedSchemeIndex_);
                targetChoice_->SetStringSelection(scheme.target);
                UpdateSchemeStatus();
                UpdateTitle();
                return;
            }
        }
    }

    void UpdateSchemeStatus()
    {
        if (!schemeStatus_ || selectedSchemeIndex_ < 0 ||
            selectedSchemeIndex_ >= static_cast<int>(projectConfig_.Schemes().size())) return;
        const auto& selected = projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)];
        const wxString target = targetChoice_ && targetChoice_->GetSelection() != wxNOT_FOUND
            ? targetChoice_->GetStringSelection() : selected.target;
        const wxString toolchain = toolchainChoice_ && toolchainChoice_->GetSelection() != wxNOT_FOUND
            ? toolchainChoice_->GetStringSelection() : selected.toolchain;
        schemeStatus_->SetLabel(wxString::Format(wxS("%s · %s · %s"), selected.configuration, target, toolchain));
    }

    void RunTask(const codium::ProjectTask& task, bool applySelectedScheme = true)
    {
        if (bottomWorkbench_) bottomWorkbench_->SetSelection(1);
        codium::ProjectTask effectiveTask = task;
        const codium::ProjectScheme* selected = nullptr;
        if (selectedSchemeIndex_ >= 0 && selectedSchemeIndex_ < static_cast<int>(projectConfig_.Schemes().size())) {
            selected = &projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)];
        }
        const wxString selectedToolchain = toolchainChoice_ && toolchainChoice_->GetSelection() != wxNOT_FOUND
            ? toolchainChoice_->GetStringSelection() : selected ? selected->toolchain : wxString(wxEmptyString);
        const wxString selectedTarget = targetChoice_ && targetChoice_->GetSelection() != wxNOT_FOUND
            ? targetChoice_->GetStringSelection() : selected ? selected->target : wxString(wxEmptyString);
        if (selected && !selected->projectFile.empty() && codeBlocksAdapter_.IsReady()) {
            if (codeBlocksAdapter_.BuildTarget(selected->projectFile, selectedTarget, selected->configuration)) {
                problemStore_.Clear(wxS("Code::Blocks adapter"));
                if (bottomWorkbench_) bottomWorkbench_->SetSelection(1);
                if (buildOutput_) buildOutput_->AppendText(wxString::Format(
                    wxS("\n=== Code::Blocks adapter: %s [%s] ===\n"), selectedTarget, selected->configuration));
                AppendLog(wxString::Format(wxS("Started Code::Blocks adapter build: %s"), selectedTarget));
                SetStatusText(wxS("Code::Blocks building"), 1);
                return;
            }
            AppendLog(wxS("Code::Blocks adapter rejected the build request; falling back to the imported task."));
        }
        if (applySelectedScheme && selected && selectedToolchain == wxS("CMake")) {
            if (effectiveTask.name.Contains(wxS("Configure"))) {
                effectiveTask.arguments.Add(wxString::Format(wxS("-DCMAKE_BUILD_TYPE=%s"), selected->configuration));
            } else if (effectiveTask.name.Contains(wxS("Build"))) {
                effectiveTask.arguments.Add(wxS("--config"));
                effectiveTask.arguments.Add(selected->configuration);
                if (selectedTarget != wxS("all")) {
                    effectiveTask.arguments.Add(wxS("--target"));
                    effectiveTask.arguments.Add(selectedTarget);
                }
            }
        }
        if (effectiveTask.name.Contains(wxS("Build")) || effectiveTask.name.Contains(wxS("Configure"))) {
            lastBuildTask_ = effectiveTask;
            hasLastBuildTask_ = true;
        }
        const wxString problemSource = effectiveTask.name;
        problemStore_.Clear(problemSource);
        if (buildOutput_) buildOutput_->AppendText(wxString::Format(wxS("\n=== %s [%s] ===\n"), effectiveTask.name,
                                                                      selected ? selected->name : wxS("default")));
        wxString error;
        if (taskRunner_.Run(effectiveTask, &error)) {
            AppendLog(wxString::Format(wxS("Started task: %s"), effectiveTask.name));
            if (buildOutput_) buildOutput_->AppendText(wxString::Format(wxS("Started %s\n"), effectiveTask.name));
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
        wxString preferredToolchain;
        if (toolchainChoice_ && toolchainChoice_->GetSelection() != wxNOT_FOUND) {
            preferredToolchain = toolchainChoice_->GetStringSelection();
        } else if (selectedSchemeIndex_ >= 0 && selectedSchemeIndex_ < static_cast<int>(projectConfig_.Schemes().size())) {
            preferredToolchain = projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)].toolchain;
        }
        for (const auto& task : projectConfig_.Tasks()) {
            if (task.name.Contains(wxS("Build")) &&
                (preferredToolchain.empty() || task.name.StartsWith(preferredToolchain + wxS(":")))) {
                RunTask(task);
                return;
            }
        }
        for (const auto& task : projectConfig_.Tasks()) {
            if (task.name.Contains(wxS("Build"))) {
                RunTask(task);
                return;
            }
        }
        AppendLog(wxS("No build task detected in this workspace."));
    }

    void RunLastBuild()
    {
        if (!EnsureWorkspaceTrusted()) return;
        if (!hasLastBuildTask_) {
            AppendLog(wxS("No previous Build or Configure task is available."));
            return;
        }
        AppendLog(wxS("Rerunning: ") + lastBuildTask_.name);
        RunTask(lastBuildTask_, false);
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
            wxS("Build project"), wxS("Rerun last build"), wxS("Run selected task"), wxS("Stop task"),
            wxS("Start terminal"), wxS("Send terminal input"), wxS("Stop terminal"), wxS("Select shell"),
            wxS("Start debug adapter"), wxS("Initialize debug"), wxS("Continue debug"),
            wxS("Launch debug program"), wxS("Pause debug"), wxS("Stop debug"),
            wxS("Start Extension Host"), wxS("Load demo extension"),
            wxS("Run hello.codium"), wxS("Stop language server"),
            wxS("Install VSIX"), wxS("List installed extensions"), wxS("Discover Code::Blocks SDK"),
            wxS("Start Code::Blocks adapter"), wxS("Stop Code::Blocks adapter"),
            wxS("Previous problem"), wxS("Next problem")
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
        case 8: RunLastBuild(); break;
        case 9: RunSelectedTask(); break;
        case 10: StopTask(); break;
        case 11: StartTerminal(); break;
        case 12: SendTerminalInput(); break;
        case 13: StopTerminal(); break;
        case 14: SelectShell(); break;
        case 15: StartDebugAdapter(); break;
        case 16: InitializeDebug(); break;
        case 17: LaunchDebug(); break;
        case 18: ContinueDebug(); break;
        case 19: PauseDebug(); break;
        case 20: StopDebug(); break;
        case 21: StartHost(); break;
        case 22: LoadDemo(); break;
        case 23: ExecuteDemo(); break;
        case 24: StopLanguageServer(); break;
        case 25: InstallVsix(); break;
        case 26: ListExtensions(); break;
        case 27: DiscoverCodeBlocks(); break;
        case 28: StartCodeBlocksAdapter(); break;
        case 29: StopCodeBlocksAdapter(); break;
        case 30: SelectAdjacentProblem(-1); break;
        case 31: SelectAdjacentProblem(1); break;
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

        wxTextCtrl* editorPage = nullptr;
        ProblemGutter* gutterPage = nullptr;
        auto* page = CreateEditorPage(notebook_, loaded.Text(), &editorPage, &gutterPage);
        documents_[path] = loaded;
        tabPaths_.push_back(path);
        editorPages_.push_back(editorPage);
        editorGutters_.push_back(gutterPage);
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
        int pageIndex = wxNOT_FOUND;
        for (size_t index = 0; index < editorPages_.size(); ++index) {
            if (editorPages_[index] == source) {
                pageIndex = static_cast<int>(index);
                break;
            }
        }
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
            int previous = wxNOT_FOUND;
            for (size_t pageIndex = 0; pageIndex < editorPages_.size(); ++pageIndex) {
                if (editorPages_[pageIndex] == editor_) {
                    previous = static_cast<int>(pageIndex);
                    break;
                }
            }
            if (previous != wxNOT_FOUND && previous < static_cast<int>(tabPaths_.size()) && !tabPaths_[previous].empty()) {
                documents_[tabPaths_[previous]].SetText(editor_->GetValue());
            }
        }
        editor_ = index < editorPages_.size() ? editorPages_[index] : nullptr;
        if (!tabPaths_[index].empty()) {
            document_ = documents_[tabPaths_[index]];
        } else {
            document_ = codium::Document();
            documentVersion_ = 1;
        }
        languageId_ = document_.IsUntitled() ? wxS("plaintext") : LanguageIdForPath(document_.Path());
        UpdateTitle();
        RefreshGutters();
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

    void DiscoverCodeBlocks()
    {
        if (bottomWorkbench_) bottomWorkbench_->SetSelection(4);
        wxString error;
        if (!codeBlocksBridge_.Discover(&error)) {
            AppendLog(wxS("Code::Blocks SDK discovery: ") + error);
            wxDirDialog dialog(this, wxS("Choose a Code::Blocks installation or source root"), wxEmptyString,
                               wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
            if (dialog.ShowModal() != wxID_OK) return;
            codeBlocksBridge_ = codium::CodeBlocksBridge(dialog.GetPath());
            if (!codeBlocksBridge_.Discover(&error)) {
                AppendLog(wxS("Code::Blocks SDK discovery: ") + error);
                return;
            }
        }
        AppendLog(wxString::Format(wxS("Code::Blocks root: %s"), codeBlocksBridge_.Root()));
        if (!codeBlocksBridge_.SdkIncludeDirectory().empty()) {
            AppendLog(wxS("Code::Blocks SDK headers: ") + codeBlocksBridge_.SdkIncludeDirectory());
        }
        if (codeBlocksBridge_.Plugins().empty()) {
            AppendLog(wxS("No native Code::Blocks plugin libraries found in discovered directories."));
        } else {
            for (const auto& plugin : codeBlocksBridge_.Plugins()) {
                AppendLog(wxString::Format(wxS("Code::Blocks plugin: %s — %s (%s)"),
                                           plugin.title, plugin.filePath,
                                           plugin.manifestValid ? plugin.status : wxS("manifest unavailable")));
            }
        }
        AppendLog(codeBlocksBridge_.LoadPolicy());
    }

    wxString CodeBlocksProjectFile() const
    {
        if (selectedSchemeIndex_ >= 0 &&
            selectedSchemeIndex_ < static_cast<int>(projectConfig_.Schemes().size())) {
            const wxString& projectFile = projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)].projectFile;
            if (!projectFile.empty()) return projectFile;
        }
        for (const auto& task : projectConfig_.Tasks()) {
            if (!task.projectFile.empty()) return task.projectFile;
        }
        return wxEmptyString;
    }

    void StartCodeBlocksAdapter()
    {
        if (!EnsureWorkspaceTrusted()) return;
        if (!workspace_.IsOpen()) {
            AppendLog(wxS("Open a workspace before starting the Code::Blocks adapter."));
            return;
        }
        wxString executable;
        wxGetEnv(wxS("CODIUM_BLOCKS_CODEBLOCKS_ADAPTER"), &executable);
        if (executable.empty()) {
            wxFileDialog dialog(this, wxS("Choose the Code::Blocks adapter executable"), wxEmptyString, wxEmptyString,
                                wxS("Executables (*.*)|*.*"), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dialog.ShowModal() != wxID_OK) return;
            executable = dialog.GetPath();
        }
        codium::CodeBlocksHostConfiguration configuration;
        configuration.sdkRoot = codeBlocksBridge_.Root();
        configuration.projectFile = CodeBlocksProjectFile();
        wxString error;
        wxArrayString arguments;
        const wxString dataDirectory = FindCodeBlocksDataDirectory(codeBlocksBridge_);
        const wxString compilerPlugin = FindCodeBlocksCompilerPlugin(codeBlocksBridge_);
        const wxString debuggerPlugin = FindCodeBlocksDebuggerPlugin(codeBlocksBridge_);
        if (dataDirectory.empty() || compilerPlugin.empty()) {
            AppendLog(wxS("Code::Blocks adapter requires resources.zip and a matching Compiler plugin; discovery found neither complete runtime path."));
            return;
        }
        arguments.Add(wxS("--data-dir=") + dataDirectory);
        arguments.Add(wxS("--compiler-plugin=") + compilerPlugin);
        if (!debuggerPlugin.empty()) arguments.Add(wxS("--debugger-plugin=") + debuggerPlugin);
        if (!codeBlocksAdapter_.Start(executable, arguments, workspace_.RootPath(), configuration, &error)) {
            AppendLog(wxS("Code::Blocks adapter error: ") + error);
            return;
        }
        adapterProjectOpened_ = false;
        bottomWorkbench_->SetSelection(4);
        AppendLog(wxS("Code::Blocks adapter started; waiting for contract handshake."));
    }

    void StopCodeBlocksAdapter()
    {
        if (codeBlocksAdapter_.IsRunning()) {
            codeBlocksAdapter_.Stop();
            AppendLog(wxS("Code::Blocks adapter stopped."));
        }
    }

    void HandleCodeBlocksEvent(const codium::CodeBlocksHostEvent& event)
    {
        const wxString source = wxS("Code::Blocks adapter");
        switch (event.kind) {
        case codium::CodeBlocksEventKind::CompilerDiagnostic: {
            codium::Problem problem;
            problem.source = source;
            problem.message = event.message;
            problem.path = event.filePath;
            if (!problem.path.empty() && !wxFileName(problem.path).IsAbsolute()) {
                problem.path = wxFileName(WorkspaceDirectory(), problem.path).GetFullPath();
            }
            problem.line = std::max(0, event.line - 1);
            problem.column = std::max(0, event.column - 1);
            problem.endLine = problem.line;
            problem.endColumn = problem.column + 1;
            problem.severity = event.isError ? codium::ProblemSeverity::Error : codium::ProblemSeverity::Warning;
            problem.raw = event.payload;
            problemStore_.Add(problem);
            if (buildOutput_) buildOutput_->AppendText(wxString::Format(wxS("%s:%d:%d: %s\n"),
                                                                          event.filePath, event.line, event.column, event.message));
            break;
        }
        case codium::CodeBlocksEventKind::BuildStarted:
            problemStore_.Clear(source);
            if (buildOutput_) buildOutput_->AppendText(wxString::Format(wxS("\n=== Code::Blocks: %s ===\n"), event.target));
            SetStatusText(wxS("Code::Blocks building"), 1);
            break;
        case codium::CodeBlocksEventKind::BuildFinished:
            if (buildOutput_) buildOutput_->AppendText(event.message + wxS("\n"));
            SetStatusText(event.exitCode == 0 ? wxS("Code::Blocks build succeeded") : wxS("Code::Blocks build failed"), 1);
            break;
        case codium::CodeBlocksEventKind::CompilerOutput: {
            if (buildOutput_) buildOutput_->AppendText(event.message + wxS("\n"));
            const wxString problemLine = event.isError ? wxS("[stderr] ") + event.message : event.message;
            const wxString root = event.projectPath.empty()
                ? WorkspaceDirectory()
                : wxFileName(event.projectPath).GetPath();
            problemStore_.AddCompilerLine(problemLine, source, root);
            break;
        }
        case codium::CodeBlocksEventKind::ProjectOpened:
        case codium::CodeBlocksEventKind::ProjectTarget:
        case codium::CodeBlocksEventKind::ProjectClosed:
        case codium::CodeBlocksEventKind::ProjectActivated:
        case codium::CodeBlocksEventKind::ProjectSaved:
        case codium::CodeBlocksEventKind::ProjectTargetsChanged:
        case codium::CodeBlocksEventKind::ProjectFileAdded:
        case codium::CodeBlocksEventKind::ProjectFileRemoved:
        case codium::CodeBlocksEventKind::ProjectFileChanged:
        case codium::CodeBlocksEventKind::ProjectFileRenamed:
        case codium::CodeBlocksEventKind::DebugSessionStarted:
        case codium::CodeBlocksEventKind::DebugSessionStopped:
        case codium::CodeBlocksEventKind::DebugSessionPaused:
        case codium::CodeBlocksEventKind::DebugSessionContinued:
        case codium::CodeBlocksEventKind::DebugSessionCursorChanged:
        case codium::CodeBlocksEventKind::DebugSessionUpdated:
        case codium::CodeBlocksEventKind::PluginCommand:
            if (debugConsole_ && (event.kind == codium::CodeBlocksEventKind::DebugSessionStarted ||
                                  event.kind == codium::CodeBlocksEventKind::DebugSessionStopped ||
                                  event.kind == codium::CodeBlocksEventKind::DebugSessionPaused ||
                                  event.kind == codium::CodeBlocksEventKind::DebugSessionContinued ||
                                  event.kind == codium::CodeBlocksEventKind::DebugSessionCursorChanged ||
                                  event.kind == codium::CodeBlocksEventKind::DebugSessionUpdated)) {
                debugConsole_->AppendText(event.message + wxS("\n"));
            }
            break;
        }
        AppendLog(wxString::Format(wxS("adapter> %s: %s"), codium::CodeBlocksEventKindName(event.kind), event.message));
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
        if (index < 0 || index >= static_cast<int>(problemLocations_.size()) ||
            index >= static_cast<int>(problemIndices_.size())) return;
        const size_t storeIndex = problemIndices_[static_cast<size_t>(index)];
        if (storeIndex >= problemStore_.Problems().size()) return;
        const codium::Problem& selectedProblem = problemStore_.Problems()[storeIndex];
        if (bottomWorkbench_) bottomWorkbench_->SetSelection(0);
        if (!selectedProblem.path.empty() && selectedProblem.path != document_.Path() &&
            wxFileExists(selectedProblem.path)) {
            OpenDocumentPath(selectedProblem.path);
        }
        if (!editor_) return;
        const long position = editor_->XYToPosition(selectedProblem.column, selectedProblem.line);
        if (position != -1) {
            editor_->SetInsertionPoint(position);
            editor_->ShowPosition(position);
            editor_->SetFocus();
        }
    }

    void SelectAdjacentProblem(int direction)
    {
        if (!problems_ || problems_->GetCount() == 0) {
            AppendLog(wxS("No visible problems match the current filters."));
            return;
        }
        int selection = problems_->GetSelection();
        if (selection == wxNOT_FOUND) {
            selection = direction < 0 ? static_cast<int>(problems_->GetCount()) - 1 : 0;
        } else {
            selection += direction;
            if (selection < 0) selection = static_cast<int>(problems_->GetCount()) - 1;
            if (selection >= static_cast<int>(problems_->GetCount())) selection = 0;
        }
        problems_->SetSelection(selection);
        GoToProblem(selection);
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
        if (codeBlocksAdapter_.IsRunning()) {
            for (const auto& event : codeBlocksAdapter_.PollEvents()) {
                if (!codeBlocksAdapter_.IsReady()) {
                    AppendLog(wxS("Code::Blocks adapter rejected the host contract."));
                    codeBlocksAdapter_.Stop();
                    break;
                }
                HandleCodeBlocksEvent(event);
                problemsChanged = problemsChanged || event.kind == codium::CodeBlocksEventKind::CompilerDiagnostic ||
                                  event.kind == codium::CodeBlocksEventKind::CompilerOutput ||
                                  event.kind == codium::CodeBlocksEventKind::BuildStarted;
            }
            if (codeBlocksAdapter_.HandshakeReceived() && !codeBlocksAdapter_.IsReady()) {
                AppendLog(wxS("Code::Blocks adapter rejected the host contract."));
                codeBlocksAdapter_.Stop();
                adapterProjectOpened_ = false;
            }
            if (codeBlocksAdapter_.IsReady()) {
                if (!adapterProjectOpened_) {
                    const wxString projectFile = CodeBlocksProjectFile();
                    if (!projectFile.empty() && codeBlocksAdapter_.OpenProject(projectFile)) {
                        adapterProjectOpened_ = true;
                        AppendLog(wxS("Code::Blocks adapter opened ") + projectFile);
                    }
                }
                SetStatusText(wxString::Format(wxS("Code::Blocks adapter %d.%d · SDK %d.%d.%d"),
                                               codeBlocksAdapter_.ContractMajor(), codeBlocksAdapter_.ContractMinor(),
                                               codeBlocksAdapter_.SdkMajor(), codeBlocksAdapter_.SdkMinor(),
                                               codeBlocksAdapter_.SdkRelease()), 2);
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
        codeBlocksAdapter_.Stop();
        adapterProjectOpened_ = false;
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
    codium::CodeBlocksBridge codeBlocksBridge_;
    codium::CodeBlocksAdapterClient codeBlocksAdapter_;
    bool adapterProjectOpened_ = false;
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
    wxChoice* schemeChoice_ = nullptr;
    wxChoice* targetChoice_ = nullptr;
    wxChoice* toolchainChoice_ = nullptr;
    wxStaticText* schemeStatus_ = nullptr;
    int selectedSchemeIndex_ = wxNOT_FOUND;
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
    std::vector<wxTextCtrl*> editorPages_;
    std::vector<ProblemGutter*> editorGutters_;
    wxListBox* diagnostics_ = nullptr;
    wxListBox* problems_ = nullptr;
    wxStaticText* problemSummary_ = nullptr;
    wxChoice* problemSeverityChoice_ = nullptr;
    wxChoice* problemSourceChoice_ = nullptr;
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
    std::vector<size_t> problemIndices_;
    codium::ProjectTask lastBuildTask_;
    bool hasLastBuildTask_ = false;

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

// wxIMPLEMENT_APP intentionally emits a helper accessor that is not referenced
// in this translation unit. AppleClang diagnoses that generated helper while
// GCC normally does not; keep the warning focused on project code.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
wxIMPLEMENT_APP(CodiumBlocksApp);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
