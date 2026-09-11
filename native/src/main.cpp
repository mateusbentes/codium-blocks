#include "codium/document.hpp"
#include "codium/editor_actions.hpp"
#include "codium/extension_host_client.hpp"
#include "codium/project_config.hpp"
#include "codium/build_session.hpp"
#include "codium/task_runner.hpp"
#include "codium/terminal_session.hpp"
#include "codium/terminal_screen.hpp"
#include "codium/terminal_profile.hpp"
#include "codium/dap_client.hpp"
#include "codium/debug_model.hpp"
#include "codium/lsp_navigation.hpp"
#include "codium/native_contributions.hpp"
#include "codium/extension_registry.hpp"
#include "codium/codeblocks_bridge.hpp"
#include "codium/codeblocks_adapter_client.hpp"
#include "codium/problem_model.hpp"
#include "codium/semantic_tokens.hpp"
#include "codium/syntax_highlighting.hpp"
#include "codium/theme.hpp"
#include "codium/localization.hpp"
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
#include <cctype>
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
    ID_FIND_EDITOR,
    ID_REPLACE_EDITOR,
    ID_REPLACE_ALL_EDITOR,
    ID_FIND_NEXT,
    ID_FIND_PREVIOUS,
    ID_GOTO_LINE,
    ID_GO_TO_FILE,
    ID_GO_TO_SYMBOL,
    ID_GO_TO_DEFINITION,
    ID_GO_TO_DECLARATION,
    ID_FIND_REFERENCES,
    ID_RENAME_SYMBOL,
    ID_CODE_ACTIONS,
    ID_NEXT_TAB,
    ID_PREVIOUS_TAB,
    ID_THEME_SYSTEM,
    ID_THEME_LIGHT,
    ID_THEME_DARK,
    ID_THEME_HIGH_CONTRAST,
    ID_LANGUAGE_SYSTEM,
    ID_LANGUAGE_ENGLISH,
    ID_LANGUAGE_PORTUGUESE_BRAZIL,
    ID_ABOUT,
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

wxString UserDataRoot()
{
    wxString root;
    if (wxGetEnv(wxS("CODIUM_BLOCKS_DATA"), &root) && !root.empty()) return root;
#if defined(__WXMSW__)
    return wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + wxS("CodiumBlocks");
#elif defined(__WXMAC__)
    return wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + wxS("CodiumBlocks");
#else
    wxString state;
    if (wxGetEnv(wxS("XDG_STATE_HOME"), &state) && !state.empty()) {
        return state + wxFILE_SEP_PATH + wxS("codium-blocks");
    }
    return wxStandardPaths::Get().GetUserConfigDir() + wxFILE_SEP_PATH + wxS("codium-blocks");
#endif
}

wxString ExtensionInstallRoot()
{
    const wxString root = UserDataRoot() + wxFILE_SEP_PATH + wxS("extensions-installed");
    wxFileName::Mkdir(root, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return root;
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

wxString FindCodeBlocksDebuggerProvider(const wxString& adapterExecutable)
{
    wxString configured;
    wxGetEnv(wxS("CODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PROVIDER"), &configured);
    if (!configured.empty() && wxFileExists(configured)) return wxFileName(configured).GetFullPath();

    const wxString directory = wxFileName(adapterExecutable).GetPath();
    const wxString names[] = {
        wxS("codium-blocks-codeblocks-debuggergdb-provider.so"),
        wxS("codium-blocks-codeblocks-debuggergdb-provider.dylib"),
        wxS("codium-blocks-codeblocks-debuggergdb-provider.dll")
    };
    for (const auto& name : names) {
        const wxString candidate = directory + wxFILE_SEP_PATH + name;
        if (wxFileExists(candidate)) return wxFileName(candidate).GetFullPath();
    }
    return wxEmptyString;
}

wxString DetectProjectRoot()
{
    const wxString executable = wxStandardPaths::Get().GetExecutablePath();
    const wxString executableDirectory = wxFileName(executable).GetPath();
    const wxString parent = ParentDirectory(executableDirectory);
    const wxString sharedResources = wxS("share") + wxString(wxFILE_SEP_PATH) + wxS("codium-blocks");
    const wxString macResources = wxS("Resources") + wxString(wxFILE_SEP_PATH) + wxS("codium-blocks");
    const wxString candidates[] = {
        executableDirectory,
        parent,
        executableDirectory + wxFILE_SEP_PATH + wxS("..") + wxFILE_SEP_PATH + sharedResources,
        parent + wxFILE_SEP_PATH + sharedResources,
        executableDirectory + wxFILE_SEP_PATH + wxS("..") + wxFILE_SEP_PATH + macResources,
        executableDirectory + wxFILE_SEP_PATH + wxS("..") + wxFILE_SEP_PATH + wxS("Resources")
    };
    for (const auto& candidate : candidates) {
        if (wxDirExists(candidate + wxFILE_SEP_PATH + wxS("extension-host"))) {
            return wxFileName(candidate).GetFullPath();
        }
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

bool JsonBoolField(const wxString& line, const wxString& field, bool fallback = false)
{
    const wxString marker = wxString::Format(wxS("\"%s\":"), field);
    const int start = line.Find(marker);
    if (start == wxNOT_FOUND) return fallback;
    const wxString value = line.Mid(start + static_cast<int>(marker.length())).Strip(wxString::both);
    if (value.StartsWith(wxS("true"))) return true;
    if (value.StartsWith(wxS("false"))) return false;
    return fallback;
}

std::vector<bool> JsonBoolFields(const wxString& line, const wxString& field)
{
    std::vector<bool> values;
    const wxString marker = wxString::Format(wxS("\"%s\":"), field);
    int searchFrom = 0;
    while (searchFrom < static_cast<int>(line.length())) {
        const int relativeStart = line.Mid(searchFrom).Find(marker);
        if (relativeStart == wxNOT_FOUND) break;
        int index = searchFrom + relativeStart + static_cast<int>(marker.length());
        while (index < static_cast<int>(line.length()) && (line[index] == wxChar(' ') || line[index] == wxChar('\t'))) ++index;
        if (line.Mid(index).StartsWith(wxS("true"))) values.push_back(true);
        else if (line.Mid(index).StartsWith(wxS("false"))) values.push_back(false);
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

struct GutterBreakpointMarker final {
    int line = 0;
    codium::DapBreakpointState state = codium::DapBreakpointState::Pending;
};

class ProblemGutter final : public wxPanel {
public:
    ProblemGutter(wxWindow* parent, const wxString& tooltip)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(58, -1), wxBORDER_NONE)
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetToolTip(tooltip);
        Bind(wxEVT_PAINT, &ProblemGutter::OnPaint, this);
        Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    }

    void SetTheme(const codium::ThemePalette& palette)
    {
        palette_ = palette;
        SetBackgroundColour(palette_.editor);
        SetForegroundColour(palette_.editorMutedText);
        Refresh();
    }

    void SetMarkers(const std::vector<GutterMarker>& markers)
    {
        markers_ = markers;
        Refresh();
    }

    void SetBreakpointMarkers(const std::vector<GutterBreakpointMarker>& markers)
    {
        breakpointMarkers_ = markers;
        Refresh();
    }

    int LineAt(int y) const
    {
        wxClientDC dc(const_cast<ProblemGutter*>(this));
        dc.SetFont(GetFont());
        const int lineHeight = std::max(1, dc.GetCharHeight() + 2);
        return firstLine_ + std::max(0, y) / lineHeight;
    }

    void SetFirstLine(int firstLine)
    {
        firstLine_ = std::max(0, firstLine);
        Refresh();
    }

private:
    wxColour MarkerColour(codium::ProblemSeverity severity) const
    {
        if (severity == codium::ProblemSeverity::Error) return palette_.error;
        if (severity == codium::ProblemSeverity::Warning) return palette_.warning;
        if (severity == codium::ProblemSeverity::Hint) return palette_.hint;
        return palette_.information;
    }

    wxColour BreakpointColour(codium::DapBreakpointState state) const
    {
        switch (state) {
        case codium::DapBreakpointState::Verified: return palette_.breakpointVerified;
        case codium::DapBreakpointState::Rejected: return palette_.breakpointRejected;
        case codium::DapBreakpointState::Disabled: return palette_.editorMutedText;
        case codium::DapBreakpointState::Pending: return palette_.breakpointPending;
        }
        return palette_.editorMutedText;
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
            dc.SetTextForeground(palette_.editorMutedText);
            dc.DrawText(number, std::max(20, GetClientSize().GetWidth() - width - 6), line * lineHeight);
            for (const auto& marker : breakpointMarkers_) {
                if (marker.line != documentLine) continue;
                dc.SetBrush(wxBrush(BreakpointColour(marker.state)));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawCircle(8, line * lineHeight + lineHeight / 2, 5);
                dc.SetTextForeground(palette_.dark ? *wxBLACK : *wxWHITE);
                dc.DrawText(wxS("B"), 4, line * lineHeight + 1);
                break;
            }
            for (const auto& marker : markers_) {
                if (marker.line != documentLine) continue;
                dc.SetBrush(wxBrush(MarkerColour(marker.severity)));
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.DrawCircle(20, line * lineHeight + lineHeight / 2, 4);
                dc.SetTextForeground(palette_.dark ? *wxBLACK : *wxWHITE);
                const wxString glyph = marker.severity == codium::ProblemSeverity::Error ? wxS("E") :
                                       marker.severity == codium::ProblemSeverity::Warning ? wxS("W") :
                                       marker.severity == codium::ProblemSeverity::Hint ? wxS("H") : wxS("I");
                dc.DrawText(glyph, 17, line * lineHeight + 1);
                break;
            }
        }
    }

    std::vector<GutterMarker> markers_;
    std::vector<GutterBreakpointMarker> breakpointMarkers_;
    codium::ThemePalette palette_ = codium::ThemePalette::For(codium::ThemeKind::System);
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

enum class DapAdvancedBreakpointRequestKind {
    Function,
    Data,
};

enum class LspResultMode {
    None,
    Completion,
    Locations,
    CodeActions,
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
          extensions_(ExtensionInstallRoot()),
          codeBlocksAdapter_(this),
          timer_(this)
    {
        wxString localizationError;
        if (!localization_.Load(projectRoot_, UserDataRoot(), &localizationError) && !localizationError.empty()) {
            // Keep the native UI usable in an incomplete development tree; the catalog reports the failure.
            wxLogWarning(wxS("%s"), localizationError);
        }
        wxString configuredTheme;
        if (wxGetEnv(wxS("CODIUM_BLOCKS_THEME"), &configuredTheme)) {
            themeKind_ = codium::ThemePalette::FromName(configuredTheme);
        }
        BuildMenuBar();
        treeRegistry_.Register(wxS("workspace"), T(wxS("label.workspace"), wxS("Workspace")));
        customEditors_.Register(wxS("json"), T(wxS("editor.json"), wxS("JSON text editor")));
        customEditors_.Register(wxS("md"), T(wxS("editor.markdown"), wxS("Markdown text editor")));
        terminalProfile_ = codium::TerminalProfileStore::Load();
        if (!terminalProfile_.shell.empty()) terminalShell_ = terminalProfile_.shell;
        terminalColumns_ = std::max(20, terminalProfile_.columns);
        terminalRows_ = std::max(4, terminalProfile_.rows);
        for (const auto& command : terminalProfile_.history) terminalHistory_.push_back(command);
        historyIndex_ = terminalHistory_.size();
        auto* root = new wxBoxSizer(wxVERTICAL);
        auto* title = new wxStaticText(this, wxID_ANY, T(wxS("title.main")));
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
        auto* navigatorTitle = new wxStaticText(navigatorPanel, wxID_ANY, T(wxS("title.projectNavigator")));
        navigatorTitle->SetFont(navigatorTitle->GetFont().Bold());
        navigatorRoot->Add(navigatorTitle, 0, wxALL | wxEXPAND, 6);
        auto* workspaceControls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(workspaceControls, T(wxS("button.open")), [this](wxCommandEvent&) { OpenWorkspace(); }, navigatorPanel);
        AddButton(workspaceControls, T(wxS("button.scm")), [this](wxCommandEvent&) { RefreshScm(); }, navigatorPanel);
        navigatorRoot->Add(workspaceControls, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);
        fileTree_ = new wxTreeCtrl(navigatorPanel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   wxTR_DEFAULT_STYLE | wxTR_SINGLE);
        fileTree_->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent& event) { OpenTreeItem(event); });
        navigatorRoot->Add(fileTree_, 1, wxLEFT | wxRIGHT | wxEXPAND, 6);
        navigatorRoot->Add(new wxStaticText(navigatorPanel, wxID_ANY, T(wxS("label.treeViews"))), 0, wxLEFT | wxRIGHT | wxTOP, 6);
        treeViewsList_ = new wxListBox(navigatorPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 50));
        navigatorRoot->Add(treeViewsList_, 0, wxLEFT | wxRIGHT | wxEXPAND, 6);
        navigatorRoot->Add(new wxStaticText(navigatorPanel, wxID_ANY, T(wxS("label.sourceControl"))), 0, wxLEFT | wxRIGHT | wxTOP, 6);
        scm_ = new wxListBox(navigatorPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 60));
        navigatorRoot->Add(scm_, 0, wxLEFT | wxRIGHT | wxEXPAND, 6);
        navigatorRoot->Add(new wxStaticText(navigatorPanel, wxID_ANY, T(wxS("label.customEditors"))), 0, wxLEFT | wxRIGHT | wxTOP, 6);
        customEditorsView_ = new wxListBox(navigatorPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 48));
        for (const auto& entry : customEditors_.Entries()) customEditorsView_->Append(entry);
        navigatorRoot->Add(customEditorsView_, 0, wxLEFT | wxRIGHT | wxEXPAND, 6);
        navigatorRoot->Add(new wxStaticText(navigatorPanel, wxID_ANY, T(wxS("label.tasks"))), 0, wxLEFT | wxRIGHT | wxTOP, 6);
        taskList_ = new wxListBox(navigatorPanel, wxID_ANY, wxDefaultPosition, wxSize(-1, 70));
        navigatorRoot->Add(taskList_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);
        navigatorPanel->SetSizer(navigatorRoot);

        auto* centerRoot = new wxBoxSizer(wxVERTICAL);
        auto* schemeBar = new wxBoxSizer(wxHORIZONTAL);
        schemeBar->Add(new wxStaticText(centerPanel, wxID_ANY, T(wxS("label.scheme"))), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        schemeChoice_ = new wxChoice(centerPanel, wxID_ANY);
        schemeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SelectScheme(); });
        schemeBar->Add(schemeChoice_, 0, wxRIGHT, 8);
        schemeBar->Add(new wxStaticText(centerPanel, wxID_ANY, T(wxS("label.target"))), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        targetChoice_ = new wxChoice(centerPanel, wxID_ANY);
        targetChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { UpdateSchemeStatus(); UpdateTitle(); });
        schemeBar->Add(targetChoice_, 0, wxRIGHT, 8);
        schemeBar->Add(new wxStaticText(centerPanel, wxID_ANY, T(wxS("label.toolchain"))), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
        toolchainChoice_ = new wxChoice(centerPanel, wxID_ANY);
        toolchainChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { SelectToolchain(); });
        schemeBar->Add(toolchainChoice_, 0, wxRIGHT, 8);
        schemeStatus_ = new wxStaticText(centerPanel, wxID_ANY, T(wxS("status.noProjectScheme")));
        schemeBar->Add(schemeStatus_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
        centerRoot->Add(schemeBar, 0, wxBOTTOM | wxEXPAND, 6);

        auto* commandBar = new wxBoxSizer(wxHORIZONTAL);
        AddButton(commandBar, T(wxS("button.openFile")), [this](wxCommandEvent&) { OpenFile(); }, centerPanel);
        AddButton(commandBar, T(wxS("button.save")), [this](wxCommandEvent&) { SaveFile(); }, centerPanel);
        AddButton(commandBar, T(wxS("button.build")), [this](wxCommandEvent&) { BuildProject(); }, centerPanel);
        AddButton(commandBar, T(wxS("button.buildAndRun")), [this](wxCommandEvent&) { RunSelectedTarget(); }, centerPanel);
        AddButton(commandBar, T(wxS("button.runTask")), [this](wxCommandEvent&) { RunSelectedTask(); }, centerPanel);
        AddButton(commandBar, T(wxS("button.palette")), [this](wxCommandEvent&) { ShowCommandPalette(); }, centerPanel);
        centerRoot->Add(commandBar, 0, wxBOTTOM | wxEXPAND, 6);

        notebook_ = new wxNotebook(centerPanel, wxID_ANY);
        ProblemGutter* initialGutter = nullptr;
        wxPanel* initialPage = CreateEditorPage(notebook_, wxEmptyString, &editor_, &initialGutter);
        notebook_->AddPage(initialPage, T(wxS("document.untitled"), wxS("Untitled")), true);
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
        problemSummary_ = new wxStaticText(problemsPage, wxID_ANY, T(wxS("status.noProblems")));
        problemsRoot->Add(problemSummary_, 0, wxALL | wxEXPAND, 6);
        auto* problemFilters = new wxBoxSizer(wxHORIZONTAL);
        problemFilters->Add(new wxStaticText(problemsPage, wxID_ANY, T(wxS("label.severity"))),
                            0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        problemSeverityChoice_ = new wxChoice(problemsPage, wxID_ANY);
        problemSeverityChoice_->Append(T(wxS("filter.all")));
        problemSeverityChoice_->Append(T(wxS("filter.errors")));
        problemSeverityChoice_->Append(T(wxS("filter.warnings")));
        problemSeverityChoice_->Append(T(wxS("filter.information")));
        problemSeverityChoice_->Append(T(wxS("filter.hints")));
        problemSeverityChoice_->SetSelection(0);
        problemSeverityChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { RefreshProblems(); });
        problemFilters->Add(problemSeverityChoice_, 0, wxRIGHT, 8);
        problemFilters->Add(new wxStaticText(problemsPage, wxID_ANY, T(wxS("label.source"))),
                            0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        problemSourceChoice_ = new wxChoice(problemsPage, wxID_ANY);
        problemSourceChoice_->Append(T(wxS("filter.allSources")));
        problemSourceChoice_->SetSelection(0);
        problemSourceChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { RefreshProblems(); });
        problemFilters->Add(problemSourceChoice_, 0, wxRIGHT, 8);
        AddButton(problemFilters, T(wxS("button.previous")), [this](wxCommandEvent&) { SelectAdjacentProblem(-1); }, problemsPage);
        AddButton(problemFilters, T(wxS("button.next")), [this](wxCommandEvent&) { SelectAdjacentProblem(1); }, problemsPage);
        AddButton(problemFilters, T(wxS("button.rerunBuild")), [this](wxCommandEvent&) { RunLastBuild(); }, problemsPage);
        problemsRoot->Add(problemFilters, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);
        problems_ = new wxListBox(problemsPage, wxID_ANY);
        problems_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) { GoToProblem(event.GetSelection()); });
        diagnostics_ = problems_;
        problemsRoot->Add(problems_, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);
        problemsPage->SetSizer(problemsRoot);
        bottomWorkbench_->AddPage(problemsPage, T(wxS("label.problems")), true);

        auto* buildPage = new wxPanel(bottomWorkbench_);
        auto* buildRoot = new wxBoxSizer(wxVERTICAL);
        auto* buildControls = new wxBoxSizer(wxHORIZONTAL);
        buildSessionStatus_ = new wxStaticText(buildPage, wxID_ANY, T(wxS("status.noBuildSession")));
        buildControls->Add(buildSessionStatus_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        AddButton(buildControls, T(wxS("button.build")), [this](wxCommandEvent&) { BuildProject(); }, buildPage);
        AddButton(buildControls, T(wxS("button.rerunBuild")), [this](wxCommandEvent&) { RunLastBuild(); }, buildPage);
        AddButton(buildControls, T(wxS("button.stop")), [this](wxCommandEvent&) { StopTask(); }, buildPage);
        buildRoot->Add(buildControls, 0, wxALL | wxEXPAND, 6);
        buildSessionList_ = new wxListBox(buildPage, wxID_ANY, wxDefaultPosition, wxSize(-1, 74));
        buildSessionList_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) {
            ShowBuildSession(event.GetSelection());
        });
        buildRoot->Add(buildSessionList_, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);
        buildOutput_ = new wxTextCtrl(buildPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);
        buildRoot->Add(buildOutput_, 1, wxALL | wxEXPAND, 6);
        buildPage->SetSizer(buildRoot);
        bottomWorkbench_->AddPage(buildPage, T(wxS("label.build")));

        auto* terminalPage = new wxPanel(bottomWorkbench_);
        auto* terminalRoot = new wxBoxSizer(wxVERTICAL);
        auto* terminalControls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(terminalControls, T(wxS("button.start")), [this](wxCommandEvent&) { StartTerminal(); }, terminalPage);
        AddButton(terminalControls, T(wxS("button.sendInput")), [this](wxCommandEvent&) { SendTerminalInput(); }, terminalPage);
        AddButton(terminalControls, T(wxS("button.stop")), [this](wxCommandEvent&) { StopTerminal(); }, terminalPage);
        AddButton(terminalControls, T(wxS("button.shell")), [this](wxCommandEvent&) { SelectShell(); }, terminalPage);
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
        bottomWorkbench_->AddPage(terminalPage, T(wxS("label.terminal")));

        auto* debugPage = new wxPanel(bottomWorkbench_);
        auto* debugRoot = new wxBoxSizer(wxVERTICAL);
        auto* debugControls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(debugControls, T(wxS("button.startAdapter")), [this](wxCommandEvent&) { StartDebugAdapter(); }, debugPage);
        AddButton(debugControls, T(wxS("button.initialize")), [this](wxCommandEvent&) { InitializeDebug(); }, debugPage);
        AddButton(debugControls, T(wxS("button.launch")), [this](wxCommandEvent&) { LaunchDebug(); }, debugPage);
        AddButton(debugControls, T(wxS("button.breakpoint")), [this](wxCommandEvent&) { ToggleBreakpoint(); }, debugPage);
        AddButton(debugControls, T(wxS("button.breakpointOptions")), [this](wxCommandEvent&) { ConfigureBreakpoint(); }, debugPage);
        AddButton(debugControls, T(wxS("button.functionBreakpoint")), [this](wxCommandEvent&) { ConfigureFunctionBreakpoint(); }, debugPage);
        AddButton(debugControls, T(wxS("button.dataBreakpoint")), [this](wxCommandEvent&) { ConfigureDataBreakpoint(); }, debugPage);
        AddButton(debugControls, T(wxS("menu.continue")), [this](wxCommandEvent&) { ContinueDebug(); }, debugPage);
        AddButton(debugControls, T(wxS("menu.pause")), [this](wxCommandEvent&) { PauseDebug(); }, debugPage);
        AddButton(debugControls, T(wxS("button.stop")), [this](wxCommandEvent&) { StopDebug(); }, debugPage);
        AddButton(debugControls, T(wxS("button.watch")), [this](wxCommandEvent&) { AddWatch(); }, debugPage);
        AddButton(debugControls, T(wxS("button.sourceMap")), [this](wxCommandEvent&) { ConfigureSourceMap(); }, debugPage);
        AddButton(debugControls, T(wxS("button.cbDebug")), [this](wxCommandEvent&) { StartCodeBlocksDebug(); }, debugPage);
        AddButton(debugControls, T(wxS("button.cbContinue")), [this](wxCommandEvent&) { ContinueCodeBlocksDebug(); }, debugPage);
        AddButton(debugControls, T(wxS("button.cbPause")), [this](wxCommandEvent&) { PauseCodeBlocksDebug(); }, debugPage);
        AddButton(debugControls, T(wxS("button.cbStop")), [this](wxCommandEvent&) { StopCodeBlocksDebug(); }, debugPage);
        debugRoot->Add(debugControls, 0, wxBOTTOM | wxEXPAND, 4);
        debugStatus_ = new wxStaticText(debugPage, wxID_ANY, T(wxS("status.dapDisconnected")));
        debugRoot->Add(debugStatus_, 0, wxBOTTOM | wxEXPAND, 4);
        auto* debugColumns = new wxBoxSizer(wxHORIZONTAL);
        auto* debugLeft = new wxBoxSizer(wxVERTICAL);
        breakpoints_ = new wxListBox(debugPage, wxID_ANY);
        breakpoints_->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { ConfigureBreakpoint(); });
        debugLeft->Add(new wxStaticText(debugPage, wxID_ANY, T(wxS("label.breakpoints"))), 0, wxBOTTOM, 2);
        debugLeft->Add(breakpoints_, 1, wxEXPAND);
        debugThreads_ = new wxListBox(debugPage, wxID_ANY);
        debugLeft->Add(new wxStaticText(debugPage, wxID_ANY, T(wxS("label.threads"))), 0, wxTOP | wxBOTTOM, 2);
        debugLeft->Add(debugThreads_, 1, wxEXPAND);
        debugColumns->Add(debugLeft, 1, wxRIGHT | wxEXPAND, 6);
        auto* debugMiddle = new wxBoxSizer(wxVERTICAL);
        callStack_ = new wxListBox(debugPage, wxID_ANY);
        callStack_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) { GoToStackFrame(event.GetSelection()); });
        debugMiddle->Add(new wxStaticText(debugPage, wxID_ANY, T(wxS("label.callStack"))), 0, wxBOTTOM, 2);
        debugMiddle->Add(callStack_, 1, wxEXPAND);
        watches_ = new wxListBox(debugPage, wxID_ANY);
        debugMiddle->Add(new wxStaticText(debugPage, wxID_ANY, T(wxS("label.watches"))), 0, wxTOP | wxBOTTOM, 2);
        debugMiddle->Add(watches_, 1, wxEXPAND);
        debugColumns->Add(debugMiddle, 1, wxRIGHT | wxEXPAND, 6);
        auto* debugRight = new wxBoxSizer(wxVERTICAL);
        variables_ = new wxListBox(debugPage, wxID_ANY);
        debugRight->Add(new wxStaticText(debugPage, wxID_ANY, T(wxS("label.variables"))), 0, wxBOTTOM, 2);
        debugRight->Add(variables_, 1, wxEXPAND);
        debugCapabilities_ = new wxListBox(debugPage, wxID_ANY);
        debugRight->Add(new wxStaticText(debugPage, wxID_ANY, T(wxS("label.capabilities"))), 0, wxTOP | wxBOTTOM, 2);
        debugRight->Add(debugCapabilities_, 1, wxEXPAND);
        debugColumns->Add(debugRight, 1, wxEXPAND);
        debugRoot->Add(debugColumns, 1, wxEXPAND);
        debugConsole_ = new wxTextCtrl(debugPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                       wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
        debugRoot->Add(debugConsole_, 0, wxTOP | wxEXPAND, 4);
        debugPage->SetSizer(debugRoot);
        bottomWorkbench_->AddPage(debugPage, T(wxS("label.debug")));

        auto* outputPage = new wxPanel(bottomWorkbench_);
        auto* outputRoot = new wxBoxSizer(wxVERTICAL);
        auto* fileControls = new wxBoxSizer(wxHORIZONTAL);
        AddButton(fileControls, T(wxS("button.startClangd")), [this](wxCommandEvent&) { StartLanguageServer(); }, outputPage);
        AddButton(fileControls, T(wxS("button.initializeLsp")), [this](wxCommandEvent&) { InitializeLanguageServer(); }, outputPage);
        AddButton(fileControls, T(wxS("button.hover")), [this](wxCommandEvent&) { RequestHover(); }, outputPage);
        AddButton(fileControls, T(wxS("button.completion")), [this](wxCommandEvent&) { RequestCompletion(); }, outputPage);
        AddButton(fileControls, T(wxS("button.semanticTokens")), [this](wxCommandEvent&) { RequestSemanticTokens(); }, outputPage);
        AddButton(fileControls, T(wxS("button.stopLsp")), [this](wxCommandEvent&) { StopLanguageServer(); }, outputPage);
        outputRoot->Add(fileControls, 0, wxBOTTOM | wxEXPAND, 4);
        auto* extensionControls = new wxBoxSizer(wxHORIZONTAL);
        extensionControls_ = extensionControls;
        AddButton(extensionControls, T(wxS("button.startHost")), [this](wxCommandEvent&) { StartHost(); }, outputPage);
        AddButton(extensionControls, T(wxS("button.loadDemo")), [this](wxCommandEvent&) { LoadDemo(); }, outputPage);
        AddButton(extensionControls, T(wxS("button.runDemo")), [this](wxCommandEvent&) { ExecuteDemo(); }, outputPage);
        AddButton(extensionControls, T(wxS("button.installVsix")), [this](wxCommandEvent&) { InstallVsix(); }, outputPage);
        AddButton(extensionControls, T(wxS("button.searchOpenVsx")), [this](wxCommandEvent&) { SearchOpenVsx(); }, outputPage);
        AddButton(extensionControls, T(wxS("button.discoverCodeBlocks")), [this](wxCommandEvent&) { DiscoverCodeBlocks(); }, outputPage);
        AddButton(extensionControls, T(wxS("button.startCbAdapter")), [this](wxCommandEvent&) { StartCodeBlocksAdapter(); }, outputPage);
        AddButton(extensionControls, T(wxS("button.stopCbAdapter")), [this](wxCommandEvent&) { StopCodeBlocksAdapter(); }, outputPage);
        outputRoot->Add(extensionControls, 0, wxBOTTOM | wxEXPAND, 4);
        hover_ = new wxTextCtrl(outputPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 60),
                                wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
        outputRoot->Add(hover_, 0, wxBOTTOM | wxEXPAND, 4);
        completion_ = new wxListBox(outputPage, wxID_ANY, wxDefaultPosition, wxSize(-1, 55));
        completion_->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { ApplySelectedLspItem(); });
        outputRoot->Add(completion_, 0, wxBOTTOM | wxEXPAND, 4);
        log_ = new wxTextCtrl(outputPage, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxHSCROLL);
        outputRoot->Add(log_, 1, wxEXPAND);
        outputPage->SetSizer(outputRoot);
        bottomWorkbench_->AddPage(outputPage, T(wxS("label.output")));

        centerRoot->Add(bottomWorkbench_, 0, wxEXPAND | wxTOP, 6);
        centerPanel->SetSizer(centerRoot);
        SetSizer(root);
        CreateStatusBar(3);
        ApplyTheme();
        SetStatusText(T(wxS("status.ready")), 0);
        SetStatusText(T(wxS("status.noWorkspace")), 1);
        SetStatusText(T(wxS("status.utf8")), 2);
        Centre();

        Bind(wxEVT_END_PROCESS, [this](wxProcessEvent& event) { OnTaskFinished(event); }, ID_TASK_PROCESS);
        Bind(wxEVT_END_PROCESS, [this](wxProcessEvent& event) { OnTerminalFinished(event); }, ID_TERMINAL_PROCESS);
        Bind(wxEVT_END_PROCESS, [this](wxProcessEvent& event) { OnDebugFinished(event); }, ID_DAP_PROCESS);

        timer_.Start(50);
        AppendLog(T(wxS("message.nativeCoreReady")));
        AppendLog(T(wxS("message.openFileForTooling")));
        wxString codeBlocksError;
        if (codeBlocksBridge_.Discover(&codeBlocksError)) {
            AppendLog(wxString::Format(T(wxS("message.codeBlocksDetected")),
                                       codeBlocksBridge_.Root(),
                                       static_cast<unsigned long>(codeBlocksBridge_.Plugins().size())));
        } else {
            AppendLog(T(wxS("message.codeBlocksDiscovery")) + codeBlocksError);
        }
    }

private:
    wxString T(const wxString& key) const
    {
        return localization_.Text(key);
    }

    wxString T(const wxString& key, const wxString& fallback) const
    {
        return localization_.Text(key, fallback);
    }

    void SelectLanguage(codium::UiLanguage language)
    {
        wxString error;
        if (!localization_.SetLanguage(language, &error)) {
            wxMessageBox(error, T(wxS("dialog.error")), wxOK | wxICON_ERROR, this);
            return;
        }
        wxMessageBox(T(wxS("status.languageChangedRestart")), T(wxS("dialog.language")),
                     wxOK | wxICON_INFORMATION, this);
    }

    void ShowAbout()
    {
        // Product identity, copyright, license and SPDX wording are intentionally invariant.
        const wxString about = wxString::Format(
            wxS("Codium::Blocks %s\n\nNative, Electron-free, multi-language IDE with a C++/wxWidgets core.\n\n"
                "Licensed under the GNU GPL-3.0-only.\nCopyright (C) 2026 Codium::Blocks Contributors."),
            CODIUM_BLOCKS_VERSION);
        wxMessageBox(about, T(wxS("about.title")), wxOK | wxICON_INFORMATION, this);
    }

    void BuildMenuBar()
    {
        auto* menuBar = new wxMenuBar();

        auto* fileMenu = new wxMenu();
        fileMenu->Append(ID_COMMAND_PALETTE, T(wxS("menu.commandPalette")));
        fileMenu->AppendSeparator();
        fileMenu->Append(ID_OPEN_WORKSPACE, T(wxS("menu.openWorkspace")));
        fileMenu->AppendSeparator();
        fileMenu->Append(wxID_OPEN, T(wxS("menu.openFile")));
        fileMenu->Append(wxID_SAVE, T(wxS("menu.saveFile")));
        fileMenu->AppendSeparator();
        fileMenu->Append(wxID_EXIT, T(wxS("menu.exit")));
        menuBar->Append(fileMenu, T(wxS("menu.file")));

        auto* editMenu = new wxMenu();
        editMenu->Append(ID_FIND_EDITOR, T(wxS("menu.find")));
        editMenu->Append(ID_FIND_NEXT, T(wxS("menu.findNext")));
        editMenu->Append(ID_FIND_PREVIOUS, T(wxS("menu.findPrevious")));
        editMenu->Append(ID_REPLACE_EDITOR, T(wxS("menu.replace")));
        editMenu->Append(ID_REPLACE_ALL_EDITOR, T(wxS("menu.replaceAll")));
        editMenu->AppendSeparator();
        editMenu->Append(ID_GOTO_LINE, T(wxS("menu.goToLine")));
        editMenu->Append(ID_GO_TO_FILE, T(wxS("menu.goToFile")));
        editMenu->Append(ID_GO_TO_SYMBOL, T(wxS("menu.goToSymbol")));
        editMenu->Append(ID_GO_TO_DEFINITION, T(wxS("menu.goToDefinition")));
        editMenu->Append(ID_GO_TO_DECLARATION, T(wxS("menu.goToDeclaration")));
        editMenu->Append(ID_FIND_REFERENCES, T(wxS("menu.findReferences")));
        editMenu->Append(ID_RENAME_SYMBOL, T(wxS("menu.renameSymbol")));
        editMenu->Append(ID_CODE_ACTIONS, T(wxS("menu.codeActions")));
        editMenu->AppendSeparator();
        editMenu->Append(ID_NEXT_TAB, T(wxS("menu.nextTab")));
        editMenu->Append(ID_PREVIOUS_TAB, T(wxS("menu.previousTab")));
        menuBar->Append(editMenu, T(wxS("menu.edit")));

        auto* languageMenu = new wxMenu();
        languageMenu->Append(ID_START_CLANGD, T(wxS("menu.startClangd")));
        languageMenu->Append(ID_INITIALIZE_LSP, T(wxS("menu.initializeLsp")));
        languageMenu->Append(ID_HOVER, T(wxS("menu.requestHover")));
        languageMenu->Append(ID_COMPLETION, T(wxS("menu.requestCompletion")));
        languageMenu->Append(ID_STOP_LSP, T(wxS("menu.stopLsp")));
        menuBar->Append(languageMenu, T(wxS("menu.language")));

        auto* buildMenu = new wxMenu();
        buildMenu->Append(ID_BUILD_PROJECT, T(wxS("menu.buildProject")));
        buildMenu->Append(ID_RERUN_BUILD, T(wxS("menu.rerunBuild")));
        buildMenu->Append(ID_RUN_TASK, T(wxS("menu.runTask")));
        buildMenu->Append(ID_STOP_TASK, T(wxS("menu.stopTask")));
        buildMenu->AppendSeparator();
        buildMenu->Append(ID_PREVIOUS_PROBLEM, T(wxS("menu.previousProblem")));
        buildMenu->Append(ID_NEXT_PROBLEM, T(wxS("menu.nextProblem")));
        menuBar->Append(buildMenu, T(wxS("menu.build")));

        auto* terminalMenu = new wxMenu();
        terminalMenu->Append(ID_START_TERMINAL, T(wxS("menu.startTerminal")));
        terminalMenu->Append(ID_SEND_TERMINAL, T(wxS("menu.sendTerminal")));
        terminalMenu->Append(ID_STOP_TERMINAL, T(wxS("menu.stopTerminal")));
        terminalMenu->Append(ID_SELECT_SHELL, T(wxS("menu.selectShell")));
        menuBar->Append(terminalMenu, T(wxS("menu.terminal")));

        auto* debugMenu = new wxMenu();
        debugMenu->Append(ID_START_DEBUG, T(wxS("menu.startDebug")));
        debugMenu->Append(ID_DEBUG_INITIALIZE, T(wxS("menu.initializeDebug")));
        debugMenu->Append(ID_DEBUG_LAUNCH, T(wxS("menu.launchProgram")));
        debugMenu->Append(ID_DEBUG_CONTINUE, T(wxS("menu.continue")));
        debugMenu->Append(ID_DEBUG_PAUSE, T(wxS("menu.pause")));
        debugMenu->Append(ID_STOP_DEBUG, T(wxS("menu.stopDebug")));
        menuBar->Append(debugMenu, T(wxS("menu.debug")));

        auto* viewMenu = new wxMenu();
        viewMenu->AppendRadioItem(ID_THEME_SYSTEM, T(wxS("menu.systemTheme")));
        viewMenu->AppendRadioItem(ID_THEME_LIGHT, T(wxS("menu.lightTheme")));
        viewMenu->AppendRadioItem(ID_THEME_DARK, T(wxS("menu.darkTheme")));
        viewMenu->AppendRadioItem(ID_THEME_HIGH_CONTRAST, T(wxS("menu.highContrastTheme")));
        auto* uiLanguageMenu = new wxMenu();
        uiLanguageMenu->AppendRadioItem(ID_LANGUAGE_SYSTEM, T(wxS("menu.languageSystem")));
        uiLanguageMenu->AppendRadioItem(ID_LANGUAGE_ENGLISH, T(wxS("menu.languageEnglish")));
        uiLanguageMenu->AppendRadioItem(ID_LANGUAGE_PORTUGUESE_BRAZIL, T(wxS("menu.languagePortugueseBrazil")));
        viewMenu->AppendSubMenu(uiLanguageMenu, T(wxS("menu.language")));
        menuBar->Append(viewMenu, T(wxS("menu.view")));

        auto* extensionMenu = new wxMenu();
        extensionMenu->Append(ID_START_HOST, T(wxS("menu.startHost")));
        extensionMenu->Append(ID_LOAD_DEMO, T(wxS("menu.loadDemo")));
        extensionMenu->Append(ID_RUN_DEMO, T(wxS("menu.runDemo")));
        extensionMenu->Append(ID_INSTALL_VSIX, T(wxS("menu.installVsix")));
        extensionMenu->Append(ID_LIST_EXTENSIONS, T(wxS("menu.listExtensions")));
        extensionMenu->Append(ID_DISCOVER_CODEBLOCKS, T(wxS("menu.discoverCodeBlocks")));
        extensionMenu->Append(ID_START_CODEBLOCKS_ADAPTER, T(wxS("menu.startCodeBlocksAdapter")));
        extensionMenu->Append(ID_STOP_CODEBLOCKS_ADAPTER, T(wxS("menu.stopCodeBlocksAdapter")));
        menuBar->Append(extensionMenu, T(wxS("menu.extensions")));

        auto* helpMenu = new wxMenu();
        helpMenu->Append(ID_ABOUT, T(wxS("menu.about")));
        menuBar->Append(helpMenu, T(wxS("menu.help")));

        SetMenuBar(menuBar);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenFile(); }, wxID_OPEN);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowCommandPalette(); }, ID_COMMAND_PALETTE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenWorkspace(); }, ID_OPEN_WORKSPACE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SaveFile(); }, wxID_SAVE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(true); }, wxID_EXIT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindInEditor(false); }, ID_FIND_EDITOR);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindNextInEditor(false); }, ID_FIND_NEXT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindNextInEditor(true); }, ID_FIND_PREVIOUS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ReplaceInEditor(); }, ID_REPLACE_EDITOR);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ReplaceAllInEditor(); }, ID_REPLACE_ALL_EDITOR);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { GoToLine(); }, ID_GOTO_LINE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { GoToFile(); }, ID_GO_TO_FILE);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RequestDocumentSymbols(); }, ID_GO_TO_SYMBOL);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RequestDefinition(); }, ID_GO_TO_DEFINITION);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RequestDeclaration(); }, ID_GO_TO_DECLARATION);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RequestReferences(); }, ID_FIND_REFERENCES);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RenameSymbol(); }, ID_RENAME_SYMBOL);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { RequestCodeActions(); }, ID_CODE_ACTIONS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SwitchAdjacentTab(1); }, ID_NEXT_TAB);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SwitchAdjacentTab(-1); }, ID_PREVIOUS_TAB);
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
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SetTheme(codium::ThemeKind::System); }, ID_THEME_SYSTEM);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SetTheme(codium::ThemeKind::Light); }, ID_THEME_LIGHT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SetTheme(codium::ThemeKind::Dark); }, ID_THEME_DARK);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SetTheme(codium::ThemeKind::HighContrast); }, ID_THEME_HIGH_CONTRAST);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SelectLanguage(codium::UiLanguage::System); }, ID_LANGUAGE_SYSTEM);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SelectLanguage(codium::UiLanguage::English); }, ID_LANGUAGE_ENGLISH);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { SelectLanguage(codium::UiLanguage::PortugueseBrazil); }, ID_LANGUAGE_PORTUGUESE_BRAZIL);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowAbout(); }, ID_ABOUT);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StartHost(); }, ID_START_HOST);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { LoadDemo(); }, ID_LOAD_DEMO);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ExecuteDemo(); }, ID_RUN_DEMO);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { InstallVsix(); }, ID_INSTALL_VSIX);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { ListExtensions(); }, ID_LIST_EXTENSIONS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { DiscoverCodeBlocks(); }, ID_DISCOVER_CODEBLOCKS);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StartCodeBlocksAdapter(); }, ID_START_CODEBLOCKS_ADAPTER);
        Bind(wxEVT_MENU, [this](wxCommandEvent&) { StopCodeBlocksAdapter(); }, ID_STOP_CODEBLOCKS_ADAPTER);

        wxAcceleratorEntry accelerators[] = {
            wxAcceleratorEntry(wxACCEL_CTRL, wxKeyCode('F'), ID_FIND_EDITOR),
            wxAcceleratorEntry(wxACCEL_CTRL, wxKeyCode('H'), ID_REPLACE_EDITOR),
            wxAcceleratorEntry(wxACCEL_CTRL, wxKeyCode('G'), ID_GOTO_LINE),
            wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F3, ID_FIND_NEXT),
            wxAcceleratorEntry(wxACCEL_SHIFT, WXK_F3, ID_FIND_PREVIOUS),
            wxAcceleratorEntry(wxACCEL_CTRL, wxKeyCode('P'), ID_GO_TO_FILE),
            wxAcceleratorEntry(wxACCEL_CTRL | wxACCEL_SHIFT, wxKeyCode('O'), ID_GO_TO_SYMBOL),
            wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F12, ID_GO_TO_DEFINITION),
            wxAcceleratorEntry(wxACCEL_SHIFT, WXK_F12, ID_FIND_REFERENCES),
            wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F2, ID_RENAME_SYMBOL),
            wxAcceleratorEntry(wxACCEL_CTRL, wxKeyCode('.'), ID_CODE_ACTIONS),
            wxAcceleratorEntry(wxACCEL_CTRL, WXK_TAB, ID_NEXT_TAB),
            wxAcceleratorEntry(wxACCEL_CTRL | wxACCEL_SHIFT, WXK_TAB, ID_PREVIOUS_TAB),
        };
        SetAcceleratorTable(wxAcceleratorTable(
            static_cast<int>(sizeof(accelerators) / sizeof(accelerators[0])), accelerators));
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
        auto* gutter = new ProblemGutter(page, T(wxS("accessibility.gutterTooltip")));
        auto* editor = new wxTextCtrl(page, wxID_ANY, text, wxDefaultPosition, wxDefaultSize,
                                      wxTE_MULTILINE | wxTE_RICH2 | wxHSCROLL);
        editor->SetFont(wxFont(11, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        gutter->Bind(wxEVT_LEFT_DOWN, [this, gutter](wxMouseEvent& event) {
            if (editor_ != nullptr && gutter != nullptr) {
                const int line = gutter->LineAt(event.GetY()) + 1;
                ToggleBreakpointAt(line);
            }
            event.Skip(false);
        });
        editor->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
            OnEditorChanged(static_cast<wxTextCtrl*>(event.GetEventObject()));
        });
        editor->Bind(wxEVT_CHAR, [this](wxKeyEvent& event) {
            auto* source = static_cast<wxTextCtrl*>(event.GetEventObject());
            if (source != editor_) {
                event.Skip();
                return;
            }
            const long caret = source->GetInsertionPoint();
            const int key = event.GetUnicodeKey() == WXK_NONE ? event.GetKeyCode() : event.GetUnicodeKey();
            if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
                const wxString indentation = codium::EditorActions::IndentationForNewline(source->GetValue(), caret);
                source->WriteText(wxS("\n") + indentation);
                return;
            }
            const wxChar typed = static_cast<wxChar>(key);
            const wxChar closing = typed == wxChar('(') ? wxChar(')') :
                                  typed == wxChar('[') ? wxChar(']') :
                                  typed == wxChar('{') ? wxChar('}') :
                                  typed == wxChar('"') ? wxChar('"') :
                                  typed == wxChar('\'') ? wxChar('\'') : wxChar();
            if (closing != wxChar()) {
                long selectionStart = 0;
                long selectionEnd = 0;
                source->GetSelection(&selectionStart, &selectionEnd);
                (void)selectionEnd;
                source->WriteText(wxString(typed) + wxString(closing));
                source->SetInsertionPoint(selectionStart + 1);
                source->SetSelection(selectionStart + 1, selectionStart + 1);
                return;
            }
            if ((typed == wxChar(')') || typed == wxChar(']') || typed == wxChar('}')) &&
                caret < source->GetLastPosition() && source->GetValue()[static_cast<size_t>(caret)] == typed) {
                source->SetInsertionPoint(caret + 1);
                return;
            }
            event.Skip();
        });
        editor->Bind(wxEVT_KEY_UP, [this](wxKeyEvent& event) {
            ApplyInlineProblems();
            event.Skip();
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
                style.SetTextColour(firstSelected ? themePalette_.editorText :
                                    first.hyperlink.empty() ? codium::TerminalScreen::PaletteColor(foreground, first.bold)
                                                             : themePalette_.accent);
                style.SetBackgroundColour(firstSelected ? themePalette_.selection : themePalette_.editor);
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

    void ApplyThemeToWindow(wxWindow* window)
    {
        if (!window) return;
        window->SetBackgroundColour(themePalette_.panel);
        window->SetForegroundColour(themePalette_.editorText);
        if (auto* gutter = dynamic_cast<ProblemGutter*>(window)) {
            gutter->SetTheme(themePalette_);
        }
        if (window == editor_) {
            editor_->SetBackgroundColour(themePalette_.editor);
            editor_->SetForegroundColour(themePalette_.editorText);
            wxTextAttr defaultStyle;
            defaultStyle.SetTextColour(themePalette_.editorText);
            defaultStyle.SetBackgroundColour(themePalette_.editor);
            editor_->SetDefaultStyle(defaultStyle);
        }
        for (wxWindow* child : window->GetChildren()) ApplyThemeToWindow(child);
        window->Refresh();
    }

    void ApplyTheme()
    {
        themePalette_ = codium::ThemePalette::For(themeKind_);
        ApplyThemeToWindow(this);
        if (terminalOutput_) {
            terminalOutput_->SetBackgroundColour(themePalette_.editor);
            terminalOutput_->SetForegroundColour(themePalette_.editorText);
        }
        if (editor_) ApplyInlineProblems();
        if (GetMenuBar()) {
            const int ids[] = {ID_THEME_SYSTEM, ID_THEME_LIGHT, ID_THEME_DARK, ID_THEME_HIGH_CONTRAST};
            const int selected = themeKind_ == codium::ThemeKind::System ? 0 :
                                 themeKind_ == codium::ThemeKind::Light ? 1 :
                                 themeKind_ == codium::ThemeKind::Dark ? 2 : 3;
            for (int index = 0; index < 4; ++index) {
                if (wxMenuItem* item = GetMenuBar()->FindItem(ids[index])) item->Check(index == selected);
            }
        }
        SetStatusText(wxString::Format(T(wxS("status.theme")), LocalizedThemeName(themeKind_),
                                       themePalette_.highContrast ? T(wxS("status.accessibleContrast")) : wxString(wxEmptyString)), 0);
    }

    void SetTheme(codium::ThemeKind kind)
    {
        themeKind_ = kind;
        ApplyTheme();
        AppendLog(T(wxS("message.themeChanged")) + LocalizedThemeName(themeKind_) + wxS("."));
    }

    wxColour ProblemColour(codium::ProblemSeverity severity) const
    {
        if (severity == codium::ProblemSeverity::Error) return themePalette_.error;
        if (severity == codium::ProblemSeverity::Warning) return themePalette_.warning;
        if (severity == codium::ProblemSeverity::Hint) return themePalette_.hint;
        return themePalette_.information;
    }

    wxTextAttr SyntaxStyle(codium::SyntaxTokenKind kind) const
    {
        wxTextAttr style;
        switch (kind) {
        case codium::SyntaxTokenKind::Comment:
            style.SetTextColour(themePalette_.editorMutedText);
            style.SetFontStyle(wxFONTSTYLE_ITALIC);
            break;
        case codium::SyntaxTokenKind::String:
            style.SetTextColour(themePalette_.dark ? wxColour(145, 220, 150) : wxColour(30, 125, 55));
            break;
        case codium::SyntaxTokenKind::Number:
            style.SetTextColour(themePalette_.dark ? wxColour(255, 205, 130) : wxColour(150, 95, 25));
            break;
        case codium::SyntaxTokenKind::Keyword:
            style.SetTextColour(themePalette_.accent);
            style.SetFontWeight(wxFONTWEIGHT_BOLD);
            break;
        case codium::SyntaxTokenKind::Type:
            style.SetTextColour(themePalette_.dark ? wxColour(210, 170, 255) : wxColour(115, 65, 165));
            break;
        case codium::SyntaxTokenKind::Function:
            style.SetTextColour(themePalette_.dark ? wxColour(130, 220, 230) : wxColour(25, 115, 145));
            break;
        case codium::SyntaxTokenKind::Property:
            style.SetTextColour(themePalette_.dark ? wxColour(255, 190, 130) : wxColour(145, 75, 25));
            break;
        case codium::SyntaxTokenKind::Preprocessor:
            style.SetTextColour(themePalette_.dark ? wxColour(255, 150, 210) : wxColour(155, 55, 130));
            break;
        case codium::SyntaxTokenKind::Tag:
            style.SetTextColour(themePalette_.dark ? wxColour(140, 220, 160) : wxColour(35, 125, 75));
            break;
        case codium::SyntaxTokenKind::Heading:
            style.SetTextColour(themePalette_.accent);
            style.SetFontWeight(wxFONTWEIGHT_BOLD);
            break;
        case codium::SyntaxTokenKind::Plain:
            break;
        }
        return style;
    }

    void ApplySyntaxHighlighting()
    {
        if (!editor_) return;
        const long end = editor_->GetLastPosition();
        editor_->SetStyle(0, end, editor_->GetDefaultStyle());
        if (end <= 0 || languageId_ == wxS("plaintext")) return;
        const auto tokens = codium::SyntaxHighlighter::Tokenize(editor_->GetValue(), languageId_);
        for (const auto& token : tokens) {
            const long start = std::max(0L, token.start);
            const long finish = std::min(end, start + std::max(0L, token.length));
            if (finish > start) editor_->SetStyle(start, finish, SyntaxStyle(token.kind));
        }
    }

    void ApplyInlineProblems()
    {
        if (!editor_) return;
        ApplySyntaxHighlighting();
        const long end = editor_->GetLastPosition();
        for (const auto& token : semanticTokens_) {
            const long start = std::max(0L, token.start);
            const long finish = std::min(end, start + std::max(0L, token.length));
            if (finish > start) {
                editor_->SetStyle(start, finish,
                                  SyntaxStyle(codium::SemanticTokenDecoder::KindForType(token.type)));
            }
        }
        HighlightMatchingDelimiter();
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

    void HighlightMatchingDelimiter()
    {
        matchingDelimiterPositions_.clear();
        if (!editor_) return;
        const auto pair = codium::EditorActions::MatchingDelimiters(
            editor_->GetValue(), editor_->GetInsertionPoint());
        if (!pair.Found()) return;
        matchingDelimiterPositions_.push_back(pair.first);
        matchingDelimiterPositions_.push_back(pair.second);
        wxTextAttr style;
        style.SetBackgroundColour(themePalette_.selection);
        style.SetFontWeight(wxFONTWEIGHT_BOLD);
        editor_->SetStyle(pair.first, pair.first + 1, style);
        editor_->SetStyle(pair.second, pair.second + 1, style);
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
            problemSourceChoice_->Append(T(wxS("filter.allSources")));
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
            const wxString stale = problem.stale ? wxString(wxS(" [")) + T(wxS("status.stale")) + wxS("]") : wxString(wxEmptyString);
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
            problemSummary_->SetLabel(wxString::Format(T(wxS("status.problemSummary")),
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
            std::vector<GutterBreakpointMarker> breakpointMarkers;
            for (const auto& breakpoint : dapSession_.Breakpoints(tabPaths_[tab])) {
                const int line = breakpoint.actualLine > 0 ? breakpoint.actualLine : breakpoint.requestedLine;
                if (line > 0) breakpointMarkers.push_back(GutterBreakpointMarker{line - 1, breakpoint.state});
            }
            editorGutters_[tab]->SetBreakpointMarkers(breakpointMarkers);
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
            SetStatusText(document_.IsUntitled() ? T(wxS("document.untitled")) : document_.Path(), 0);
            wxString workspaceStatus = workspace_.IsOpen() ? (workspace_.IsTrusted() ? T(wxS("status.workspaceTrusted")) : T(wxS("status.workspaceUntrusted")))
                                                             : T(wxS("status.noWorkspace"));
            if (selectedSchemeIndex_ >= 0 && selectedSchemeIndex_ < static_cast<int>(projectConfig_.Schemes().size())) {
                workspaceStatus += wxS(" · ") + projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)].name;
            }
            SetStatusText(workspaceStatus, 1);
            SetStatusText(wxString::Format(T(wxS("status.cursorPosition")), languageId_,
                                           CurrentEditorLine() + 1, CurrentEditorCharacter() + 1), 2);
        }
    }

    void OpenFile()
    {
        wxFileDialog dialog(this, T(wxS("dialog.openSourceFile")), wxEmptyString, wxEmptyString,
                            T(wxS("dialog.allFilesFilter")), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }

        OpenDocumentPath(dialog.GetPath());
    }

    void OpenWorkspace()
    {
        wxDirDialog dialog(this, T(wxS("dialog.chooseWorkspace")), wxEmptyString,
                           wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) return;

        wxString error;
        if (!workspace_.Open(dialog.GetPath(), &error)) {
            AppendLog(T(wxS("message.errorPrefix")) + error);
            return;
        }
        if (!workspace_.IsTrusted()) {
            const int answer = wxMessageBox(
                wxString::Format(T(wxS("dialog.trustWorkspace")), workspace_.RootPath()),
                T(wxS("dialog.workspaceTrustTitle")), wxYES_NO | wxICON_WARNING, this);
            if (answer == wxYES && workspace_.SetTrusted(true, &error)) AppendLog(T(wxS("status.workspaceTrusted")) + wxS("."));
            else AppendLog(T(wxS("status.workspaceUntrusted")) + wxS("; execution features remain restricted."));
        }
        watchExpressions_ = codium::WatchStore::Load(workspace_.RootPath());
        RefreshWatchView();
        dapSession_.ReplaceBreakpoints(codium::DapBreakpointStore::Load(workspace_.RootPath()));
        wxString breakpointError;
        if (!codium::DapAdvancedBreakpointStore::Load(workspace_.RootPath(), &functionBreakpoints_,
                                                      &dataBreakpoints_, &breakpointError)) {
            AppendLog(T(wxS("message.advancedBreakpointPersistence")) + breakpointError);
        }
        RefreshBreakpointView();
        PopulateFileTree();
        RefreshNativeContributions();
        LoadProjectConfig();
        SetTitle(wxString::Format(wxS("%s — Codium::Blocks %s"), workspace_.RootPath(), CODIUM_BLOCKS_VERSION));
        UpdateTitle();
        AppendLog(T(wxS("message.workspaceOpened")) + workspace_.RootPath());
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
            scm_->Append(T(wxS("dialog.noSourceControl")));
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
            AppendLog(T(wxS("message.errorPrefix")) + error);
            return;
        }
        if (!projectConfig_.LoadPreferences(workspace_.RootPath(), &projectPreferences_, &error)) {
            AppendLog(T(wxS("message.projectPreferencesUnavailable")) + error);
        }
        if (!buildSessions_.Load(workspace_.RootPath(), &error)) {
            AppendLog(T(wxS("message.buildHistoryUnavailable")) + error);
        }
        for (const auto& toolchain : projectConfig_.Toolchains()) {
            AppendLog(T(wxS("message.detectedToolchain")) + toolchain);
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
            if (schemeStatus_) schemeStatus_->SetLabel(T(wxS("status.noProjectScheme")));
            RefreshBuildSessions();
            return;
        }
        selectedSchemeIndex_ = 0;
        if (!projectPreferences_.schemeName.empty()) {
            for (size_t index = 0; index < projectConfig_.Schemes().size(); ++index) {
                if (projectConfig_.Schemes()[index].name == projectPreferences_.schemeName) {
                    selectedSchemeIndex_ = static_cast<int>(index);
                    break;
                }
            }
        } else if (!projectPreferences_.configuration.empty() || !projectPreferences_.target.empty() ||
                   !projectPreferences_.toolchain.empty()) {
            for (size_t index = 0; index < projectConfig_.Schemes().size(); ++index) {
                const auto& candidate = projectConfig_.Schemes()[index];
                if ((!projectPreferences_.configuration.empty() && candidate.configuration != projectPreferences_.configuration) ||
                    (!projectPreferences_.target.empty() && candidate.target != projectPreferences_.target) ||
                    (!projectPreferences_.toolchain.empty() && candidate.toolchain != projectPreferences_.toolchain)) continue;
                selectedSchemeIndex_ = static_cast<int>(index);
                break;
            }
        }
        schemeChoice_->SetSelection(selectedSchemeIndex_);
        const auto& selected = projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)];
        targetChoice_->SetStringSelection(selected.target);
        toolchainChoice_->SetStringSelection(selected.toolchain);
        UpdateSchemeStatus();
        UpdateTitle();
        RefreshBuildSessions();
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
        AppendLog(wxString::Format(T(wxS("message.selectedScheme")), selected.name,
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
        if (workspace_.IsOpen()) {
            projectPreferences_.schemeName = selected.name;
            projectPreferences_.configuration = selected.configuration;
            projectPreferences_.target = target;
            projectPreferences_.toolchain = toolchain;
            wxString error;
            if (!projectConfig_.SavePreferences(workspace_.RootPath(), projectPreferences_, &error)) {
                AppendLog(T(wxS("message.projectSelectionSaveError")) + error);
            }
        }
    }

    void RefreshBuildSessions()
    {
        if (!buildSessionList_) return;
        buildSessionList_->Freeze();
        buildSessionList_->Clear();
        const auto& sessions = buildSessions_.Sessions();
        for (size_t offset = 0; offset < sessions.size(); ++offset) {
            const auto& session = sessions[sessions.size() - offset - 1];
            buildSessionList_->Append(wxString::Format(wxS("%s — %s"), session.taskName,
                                                       codium::BuildSessionStore::DisplayLabel(session)));
        }
        buildSessionList_->Thaw();
        const codium::BuildSession* current = buildSessions_.Current();
        if (buildSessionStatus_) {
            buildSessionStatus_->SetLabel(current
                ? wxString::Format(T(wxS("status.buildRunning")), current->taskName,
                                   current->configuration.empty() ? T(wxS("status.default")) : current->configuration)
                : sessions.empty() ? T(wxS("status.noBuildSession")) :
                  wxString::Format(T(wxS("status.buildLast")), codium::BuildSessionStore::DisplayLabel(sessions.back())));
        }
    }

    void ShowBuildSession(int selection)
    {
        const auto& sessions = buildSessions_.Sessions();
        if (selection < 0 || selection >= static_cast<int>(sessions.size())) return;
        const size_t index = sessions.size() - static_cast<size_t>(selection) - 1;
        const auto& session = sessions[index];
        if (buildSessionStatus_) buildSessionStatus_->SetLabel(codium::BuildSessionStore::DisplayLabel(session));
        if (buildOutput_) {
            buildOutput_->Clear();
            buildOutput_->AppendText(wxString::Format(T(wxS("status.buildHeader")), session.taskName,
                                                      session.configuration.empty() ? T(wxS("status.default")) : session.configuration,
                                                      session.target.empty() ? T(wxS("status.default")) : session.target));
            for (const auto& line : session.output) buildOutput_->AppendText(line + wxS("\n"));
        }
    }

    bool IsBuildLikeTask(const codium::ProjectTask& task) const
    {
        return task.kind == codium::ProjectTaskKind::Build ||
               task.kind == codium::ProjectTaskKind::Configure ||
               task.name.Contains(wxS("Build")) || task.name.Contains(wxS("Configure"));
    }

    void BeginBuildSession(const codium::ProjectTask& task, const codium::ProjectScheme* selected,
                           const wxString& selectedTarget, const wxString& selectedToolchain)
    {
        if (!IsBuildLikeTask(task)) return;
        codium::BuildSessionSpec specification;
        specification.taskName = task.name;
        specification.target = selectedTarget.empty() ? task.targetName : selectedTarget;
        specification.configuration = selected ? selected->configuration : wxS("Debug");
        specification.toolchain = selectedToolchain;
        if (specification.toolchain.empty() && selected) specification.toolchain = selected->toolchain;
        specification.projectFile = task.projectFile;
        specification.workingDirectory = task.workingDirectory;
        wxString error;
        activeBuildSessionId_ = buildSessions_.Begin(specification, &error);
        if (activeBuildSessionId_.empty() && !error.empty()) AppendLog(T(wxS("message.buildSessionError")) + error);
        RefreshBuildSessions();
    }

    void AppendBuildSessionOutput(const wxString& line)
    {
        if (activeBuildSessionId_.empty()) return;
        wxString error;
        if (!buildSessions_.AppendOutput(activeBuildSessionId_, line, &error)) AppendLog(error);
    }

    void FinishBuildSession(int exitCode, bool cancelled = false)
    {
        if (activeBuildSessionId_.empty()) return;
        wxString error;
        if (!buildSessions_.Finish(activeBuildSessionId_, exitCode, cancelled, &error)) AppendLog(error);
        activeBuildSessionId_.clear();
        RefreshBuildSessions();
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
        if (applySelectedScheme && selected && selectedToolchain == wxS("CMake")) {
            const bool usesPreset = effectiveTask.arguments.Index(wxS("--preset")) != wxNOT_FOUND;
            if (effectiveTask.name.Contains(wxS("Configure")) && !usesPreset) {
                effectiveTask.arguments.Add(wxString::Format(wxS("-DCMAKE_BUILD_TYPE=%s"), selected->configuration));
            } else if (effectiveTask.name.Contains(wxS("Build"))) {
                effectiveTask.arguments.Add(wxS("--config"));
                effectiveTask.arguments.Add(selected->configuration);
                if (effectiveTask.targetName.empty() && selectedTarget != wxS("all")) {
                    effectiveTask.arguments.Add(wxS("--target"));
                    effectiveTask.arguments.Add(selectedTarget);
                }
            }
        } else if (applySelectedScheme && selected && !selectedTarget.empty() &&
                   effectiveTask.targetName.empty()) {
            if (selectedToolchain == wxS("Make")) {
                effectiveTask.arguments.Add(selectedTarget);
            } else if (selectedToolchain == wxS("Cargo") && selectedTarget != wxS("workspace")) {
                effectiveTask.arguments.Add(wxS("--bin"));
                effectiveTask.arguments.Add(selectedTarget);
            } else if (selectedToolchain == wxS("npm") && selectedTarget != wxS("package")) {
                effectiveTask.arguments.Clear();
                effectiveTask.arguments.Add(wxS("run"));
                effectiveTask.arguments.Add(selectedTarget);
            }
        }
        if (IsBuildLikeTask(effectiveTask)) {
            lastBuildTask_ = effectiveTask;
            hasLastBuildTask_ = true;
        }
        BeginBuildSession(effectiveTask, selected, selectedTarget, selectedToolchain);
        const bool isBuildTask = IsBuildLikeTask(effectiveTask);
        if (isBuildTask && selected && !selected->projectFile.empty() && codeBlocksAdapter_.IsReady()) {
            if (codeBlocksAdapter_.BuildTarget(selected->projectFile, selectedTarget, selected->configuration)) {
                problemStore_.Clear(wxS("Code::Blocks adapter"));
                if (bottomWorkbench_) bottomWorkbench_->SetSelection(1);
                if (buildOutput_) buildOutput_->AppendText(wxString::Format(
                    wxS("\n=== Code::Blocks adapter: %s [%s] ===\n"), selectedTarget, selected->configuration));
                AppendLog(wxString::Format(T(wxS("message.codeBlocksBuildStarted")), selectedTarget));
                SetStatusText(T(wxS("status.codeBlocksBuilding")), 1);
                return;
            }
            AppendLog(T(wxS("message.codeBlocksBuildRejected")));
        }
        const wxString problemSource = effectiveTask.name;
        problemStore_.Clear(problemSource);
        if (buildOutput_) buildOutput_->AppendText(wxS("\n") +
            wxString::Format(T(wxS("message.buildHeader")), effectiveTask.name,
                             selected ? selected->name : T(wxS("status.default"))) + wxS("\n"));
        wxString error;
        if (taskRunner_.Run(effectiveTask, &error)) {
            AppendLog(wxString::Format(T(wxS("message.taskStarted")), effectiveTask.name));
            if (buildOutput_) buildOutput_->AppendText(wxString::Format(T(wxS("message.taskStartedLine")), effectiveTask.name) + wxS("\n"));
            if (GetStatusBar()) SetStatusText(T(wxS("status.building")), 1);
        } else {
            AppendLog(T(wxS("message.taskError")) + error);
            if (buildOutput_) buildOutput_->AppendText(T(wxS("message.taskError")) + error + wxS("\n"));
            FinishBuildSession(-1);
            pendingRunAfterBuild_ = false;
        }
        RefreshProblems();
    }

    bool EnsureWorkspaceTrusted()
    {
        if (!workspace_.IsOpen() || workspace_.IsTrusted()) return true;
        AppendLog(T(wxS("dialog.executionBlocked")));
        return false;
    }

    void BuildProject()
    {
        if (!EnsureWorkspaceTrusted()) return;
        if (!workspace_.IsOpen()) {
            AppendLog(T(wxS("dialog.openWorkspaceFirst")));
            return;
        }
        wxString preferredToolchain;
        if (toolchainChoice_ && toolchainChoice_->GetSelection() != wxNOT_FOUND) {
            preferredToolchain = toolchainChoice_->GetStringSelection();
        } else if (selectedSchemeIndex_ >= 0 && selectedSchemeIndex_ < static_cast<int>(projectConfig_.Schemes().size())) {
            preferredToolchain = projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)].toolchain;
        }
        const wxString selectedTarget = targetChoice_ && targetChoice_->GetSelection() != wxNOT_FOUND
            ? targetChoice_->GetStringSelection()
            : selectedSchemeIndex_ >= 0 && selectedSchemeIndex_ < static_cast<int>(projectConfig_.Schemes().size())
                ? projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)].target : wxString(wxEmptyString);
        if (selectedSchemeIndex_ >= 0 && selectedSchemeIndex_ < static_cast<int>(projectConfig_.Schemes().size())) {
            const auto& selectedScheme = projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)];
            if (!selectedScheme.buildTaskName.empty()) {
                for (const auto& task : projectConfig_.Tasks()) {
                    if (task.name == selectedScheme.buildTaskName) {
                        RunTask(task);
                        return;
                    }
                }
            }
        }
        const auto taskMatchesToolchain = [&preferredToolchain](const codium::ProjectTask& task) {
            if (preferredToolchain.empty()) return true;
            if (!task.toolchain.empty() && task.toolchain.Lower() == preferredToolchain.Lower()) return true;
            if (task.name.StartsWith(preferredToolchain + wxS(":"))) return true;
            return preferredToolchain.StartsWith(wxS("Code::Blocks")) && task.program == wxS("codeblocks");
        };
        for (const auto& task : projectConfig_.Tasks()) {
            if (task.targetName == selectedTarget && IsBuildLikeTask(task) && taskMatchesToolchain(task)) {
                RunTask(task);
                return;
            }
        }
        for (const auto& task : projectConfig_.Tasks()) {
            if (IsBuildLikeTask(task) && taskMatchesToolchain(task)) {
                RunTask(task);
                return;
            }
        }
        for (const auto& task : projectConfig_.Tasks()) {
            if (IsBuildLikeTask(task)) {
                RunTask(task);
                return;
            }
        }
        AppendLog(T(wxS("dialog.noBuildTask")));
    }

    const codium::ProjectTarget* SelectedProjectTarget() const
    {
        if (selectedSchemeIndex_ < 0 || selectedSchemeIndex_ >= static_cast<int>(projectConfig_.Schemes().size())) return nullptr;
        const auto& scheme = projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)];
        for (const auto& target : projectConfig_.Targets()) {
            if (target.name == scheme.target && target.toolchain == scheme.toolchain &&
                (target.projectFile.empty() || target.projectFile == scheme.projectFile)) return &target;
        }
        return nullptr;
    }

    void LaunchPendingRun()
    {
        if (!pendingRunAfterBuild_) return;
        const codium::ProjectTarget target = pendingRunTarget_;
        const wxString configuration = pendingRunConfiguration_.empty() ? wxS("Debug") : pendingRunConfiguration_;
        const wxString artifact = codium::ProjectConfig::DiscoverArtifact(target, configuration,
                                                                            pendingRunArtifactOverride_);
        codium::ProjectTask task;
        task.name = T(wxS("label.runPrefix")) + target.name;
        task.kind = codium::ProjectTaskKind::Run;
        task.toolchain = target.toolchain;
        task.workingDirectory = target.workingDirectory;
        task.projectFile = target.projectFile;
        task.targetName = target.name;

        if (!pendingRunTaskName_.empty()) {
            for (const auto& candidate : projectConfig_.Tasks()) {
                if (candidate.name == pendingRunTaskName_ && candidate.kind == codium::ProjectTaskKind::Run) {
                    task = candidate;
                    break;
                }
            }
        }
        if (task.program.empty() && target.toolchain == wxS("npm")) {
            task.program = wxS("npm");
            task.arguments.Add(wxS("run"));
            task.arguments.Add(target.name);
        } else if (task.program.empty()) {
            task.program = artifact.empty() ? target.runProgram : artifact;
            task.arguments = target.runArguments;
        }
        pendingRunAfterBuild_ = false;
        pendingRunTaskName_.clear();
        pendingRunConfiguration_.clear();
        pendingRunArtifactOverride_.clear();
        if (task.program.empty()) {
            AppendLog(T(wxS("message.noRunnableArtifact")));
            return;
        }
        AppendLog(T(wxS("message.buildRunningTarget")) + task.program);
        if (bottomWorkbench_) bottomWorkbench_->SetSelection(1);
        RunTask(task, false);
    }

    void RunSelectedTarget()
    {
        if (!EnsureWorkspaceTrusted()) return;
        const codium::ProjectTarget* target = SelectedProjectTarget();
        if (!target || !target->supportsRun) {
            AppendLog(T(wxS("dialog.noRunnableTarget")));
            return;
        }
        const codium::ProjectScheme* scheme = selectedSchemeIndex_ >= 0 &&
            selectedSchemeIndex_ < static_cast<int>(projectConfig_.Schemes().size())
                ? &projectConfig_.Schemes()[static_cast<size_t>(selectedSchemeIndex_)] : nullptr;
        pendingRunTarget_ = *target;
        pendingRunConfiguration_ = scheme ? scheme->configuration : wxS("Debug");
        pendingRunArtifactOverride_ = scheme ? scheme->artifactPath : wxString(wxEmptyString);
        pendingRunTaskName_ = scheme ? scheme->runTaskName : wxString(wxEmptyString);

        const wxString buildTaskName = scheme && !scheme->buildTaskName.empty()
            ? scheme->buildTaskName : target->buildTaskName;
        if (target->supportsBuild && !buildTaskName.empty()) {
            for (const auto& task : projectConfig_.Tasks()) {
                if (task.name == buildTaskName ||
                    (task.targetName == target->name && IsBuildLikeTask(task))) {
                    pendingRunAfterBuild_ = true;
                    AppendLog(T(wxS("message.buildingSelectedTarget")) + target->name);
                    RunTask(task);
                    return;
                }
            }
        }
        pendingRunAfterBuild_ = true;
        LaunchPendingRun();
    }

    void RunLastBuild()
    {
        if (!EnsureWorkspaceTrusted()) return;
        if (!hasLastBuildTask_) {
            AppendLog(T(wxS("dialog.noPreviousBuild")));
            return;
        }
        AppendLog(T(wxS("message.rerunning")) + lastBuildTask_.name);
        RunTask(lastBuildTask_, false);
    }

    void RunSelectedTask()
    {
        if (!EnsureWorkspaceTrusted()) return;
        const int selection = taskList_ ? taskList_->GetSelection() : wxNOT_FOUND;
        if (selection == wxNOT_FOUND || selection >= static_cast<int>(projectConfig_.Tasks().size())) {
            AppendLog(T(wxS("dialog.selectTask")));
            return;
        }
        RunTask(projectConfig_.Tasks()[selection]);
    }

    void StopTask()
    {
        if (taskRunner_.IsRunning()) {
            taskRunner_.Stop();
            FinishBuildSession(-1, true);
            pendingRunAfterBuild_ = false;
            AppendLog(T(wxS("message.taskStopped")));
        }
    }

    void OnTaskFinished(wxProcessEvent& event)
    {
        taskRunner_.HandleProcessExit(event.GetPid(), event.GetExitCode());
        FinishBuildSession(event.GetExitCode());
        AppendLog(wxString::Format(T(wxS("message.taskFinished")), event.GetExitCode()));
        if (buildOutput_) buildOutput_->AppendText(wxString::Format(T(wxS("message.finishedWithExitCode")), event.GetExitCode()) + wxS("\n"));
        if (GetStatusBar()) SetStatusText(event.GetExitCode() == 0 ? T(wxS("status.buildSucceeded")) : T(wxS("status.buildFailed")), 1);
        RefreshProblems();
        if (pendingRunAfterBuild_) {
            if (event.GetExitCode() == 0) LaunchPendingRun();
            else {
                pendingRunAfterBuild_ = false;
                pendingRunTaskName_.clear();
                pendingRunConfiguration_.clear();
                pendingRunArtifactOverride_.clear();
                AppendLog(T(wxS("message.buildFailedNotRun")));
            }
        }
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
            AppendLog(T(wxS("message.terminalAlreadyRunning")));
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
            AppendLog(T(wxS("message.nativeTerminalBackend")) + terminal_.BackendName());
            if (GetStatusBar()) SetStatusText(T(wxS("status.terminalRunning")), 1);
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
            AppendLog(T(wxS("message.terminalError")) + error);
        }
    }

    void SelectShell()
    {
        if (terminal_.IsRunning()) {
            AppendLog(T(wxS("message.stopTerminalBeforeShell")));
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
        wxSingleChoiceDialog dialog(this, T(wxS("dialog.selectShell")),
                                    T(wxS("dialog.terminalShell")), choices);
        if (dialog.ShowModal() == wxID_OK) {
            terminalShell_ = choices[dialog.GetSelection()];
            terminalProfile_.shell = terminalShell_;
            SaveTerminalProfile();
            AppendLog(T(wxS("message.selectedShell")) + terminalShell_);
        }
    }

    void SendTerminalInput()
    {
        if (!terminal_.IsRunning()) {
            AppendLog(T(wxS("message.startTerminalFirst")));
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
            AppendLog(T(wxS("message.terminalWriteError")));
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
            AppendLog(T(wxS("message.terminalStopped")));
        }
    }

    void OnTerminalFinished(wxProcessEvent& event)
    {
        terminal_.HandleProcessExit(event.GetPid(), event.GetExitCode());
        AppendLog(wxString::Format(T(wxS("message.terminalFinished")), event.GetExitCode()));
        if (GetStatusBar()) SetStatusText(event.GetExitCode() == 0 ? T(wxS("status.terminalExited")) : T(wxS("status.terminalFailed")), 1);
    }

    void StartDebugAdapter()
    {
        if (!EnsureWorkspaceTrusted()) return;
        if (dap_.IsRunning()) {
            AppendLog(T(wxS("dialog.adapterAlreadyRunning")));
            return;
        }
        wxTextEntryDialog dialog(this, T(wxS("dialog.debugAdapterExecutable")),
                                 T(wxS("dialog.debugAdapter")), wxS("codelldb"));
        if (dialog.ShowModal() != wxID_OK || dialog.GetValue().empty()) return;
        wxArrayString arguments;
        wxString error;
        supportsFunctionBreakpoints_ = false;
        supportsDataBreakpoints_ = false;
        for (auto& breakpoint : functionBreakpoints_) {
            breakpoint.state = codium::DapBreakpointState::Pending;
            breakpoint.message.clear();
            breakpoint.id = 0;
        }
        for (auto& breakpoint : dataBreakpoints_) {
            breakpoint.state = codium::DapBreakpointState::Pending;
            breakpoint.message.clear();
            breakpoint.id = 0;
        }
        dapAdvancedBreakpointRequests_.clear();
        if (dap_.Start(dialog.GetValue(), arguments, WorkspaceDirectory(), &error)) {
            dapSession_.MarkStarted();
            ClearDapTransientViews();
            UpdateDapStatus();
            RefreshBreakpointView();
            RefreshGutters();
            AppendLog(T(wxS("message.debugAdapterStarted")));
        } else {
            AppendLog(T(wxS("message.debugAdapterError")) + error);
        }
    }

    void InitializeDebug()
    {
        if (!dap_.IsRunning()) {
            AppendLog(T(wxS("dialog.startAdapterFirst")));
            return;
        }
        dapSession_.MarkInitializing();
        UpdateDapStatus();
        if (dap_.SendRequest(wxS("initialize"),
                             wxS("{\"clientID\":\"codium-blocks\",\"adapterID\":\"codium-blocks\",\"linesStartAt1\":true,\"columnsStartAt1\":true}"))) {
            AppendLog(T(wxS("message.dapInitializeSent")));
        }
    }

    void LaunchDebug()
    {
        if (!dap_.IsRunning()) {
            AppendLog(T(wxS("dialog.startAdapterFirst")));
            return;
        }
        wxString program;
        if (const codium::ProjectTarget* target = SelectedProjectTarget(); target && target->supportsDebug) {
            program = target->runProgram;
        }
        if (program.empty()) {
            wxFileDialog dialog(this, T(wxS("dialog.chooseProgram")), wxEmptyString, wxEmptyString,
                                T(wxS("dialog.executableFilter")), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dialog.ShowModal() != wxID_OK) return;
            program = dialog.GetPath();
        }
        wxString launchArguments = wxString::Format(wxS("{\"program\":\"%s\",\"cwd\":\"%s\",\"sourceFileMap\":%s}"),
                                                    JsonEscape(program), JsonEscape(WorkspaceDirectory()), SourceMapArguments());
        if (dap_.SendRequest(wxS("launch"), launchArguments)) {
            AppendLog(T(wxS("message.dapLaunchSent")));
            SendAllBreakpoints();
        }
    }

    wxString LocalizedDapBreakpointState(codium::DapBreakpointState state) const
    {
        switch (state) {
        case codium::DapBreakpointState::Pending: return T(wxS("label.breakpointPending"));
        case codium::DapBreakpointState::Verified: return T(wxS("label.breakpointVerified"));
        case codium::DapBreakpointState::Rejected: return T(wxS("label.breakpointRejected"));
        case codium::DapBreakpointState::Disabled: return T(wxS("label.breakpointDisabled"));
        }
        return T(wxS("label.breakpointUnknown"));
    }

    wxString LocalizedDapRunState(codium::DapRunState state) const
    {
        switch (state) {
        case codium::DapRunState::Disconnected: return T(wxS("label.dapDisconnected"));
        case codium::DapRunState::Initializing: return T(wxS("label.dapInitializing"));
        case codium::DapRunState::Initialized: return T(wxS("label.dapInitialized"));
        case codium::DapRunState::Running: return T(wxS("label.dapRunning"));
        case codium::DapRunState::Paused: return T(wxS("label.dapPaused"));
        case codium::DapRunState::Stopped: return T(wxS("label.dapStopped"));
        }
        return T(wxS("label.dapUnknown"));
    }

    wxString LocalizedThemeName(codium::ThemeKind kind) const
    {
        switch (kind) {
        case codium::ThemeKind::System: return T(wxS("label.themeSystem"));
        case codium::ThemeKind::Light: return T(wxS("label.themeLight"));
        case codium::ThemeKind::Dark: return T(wxS("label.themeDark"));
        case codium::ThemeKind::HighContrast: return T(wxS("label.themeHighContrast"));
        }
        return T(wxS("label.themeSystem"));
    }

    void RefreshBreakpointView()
    {
        if (!breakpoints_) return;
        breakpoints_->Clear();
        const auto& breakpoints = dapSession_.Breakpoints(document_.Path());
        for (const auto& breakpoint : breakpoints) {
            const int line = breakpoint.actualLine > 0 ? breakpoint.actualLine : breakpoint.requestedLine;
            wxString label = wxString::Format(wxS("%s:%d [%s]"), document_.Path(), line,
                                              LocalizedDapBreakpointState(breakpoint.state));
            if (!breakpoint.message.empty()) label += wxS(" — ") + breakpoint.message;
            if (!breakpoint.condition.empty()) label += T(wxS("label.conditionPrefix")) + breakpoint.condition;
            if (!breakpoint.hitCondition.empty()) label += T(wxS("label.hitPrefix")) + breakpoint.hitCondition;
            if (!breakpoint.logMessage.empty()) label += T(wxS("label.logPrefix")) + breakpoint.logMessage;
            breakpoints_->Append(label);
        }
        for (const auto& breakpoint : functionBreakpoints_) {
            wxString label = T(wxS("label.functionPrefix")) + breakpoint.name + wxS(" [") +
                             LocalizedDapBreakpointState(breakpoint.state) + wxS("]");
            if (!breakpoint.condition.empty()) label += T(wxS("label.conditionPrefix")) + breakpoint.condition;
            if (!breakpoint.hitCondition.empty()) label += T(wxS("label.hitPrefix")) + breakpoint.hitCondition;
            if (!breakpoint.message.empty()) label += wxS(" — ") + breakpoint.message;
            breakpoints_->Append(label);
        }
        for (const auto& breakpoint : dataBreakpoints_) {
            wxString label = T(wxS("label.dataPrefix")) + breakpoint.dataId + wxS(" (") + breakpoint.accessType + wxS(") [") +
                             LocalizedDapBreakpointState(breakpoint.state) + wxS("]");
            if (!breakpoint.message.empty()) label += wxS(" — ") + breakpoint.message;
            breakpoints_->Append(label);
        }
    }

    void PersistBreakpoints()
    {
        if (!workspace_.IsOpen()) return;
        wxString error;
        if (!codium::DapBreakpointStore::Save(workspace_.RootPath(), dapSession_.AllRequestedBreakpoints(), &error)) {
            AppendLog(T(wxS("message.breakpointPersistenceError")) + error);
        }
        error.clear();
        if (!codium::DapAdvancedBreakpointStore::Save(workspace_.RootPath(), functionBreakpoints_,
                                                      dataBreakpoints_, &error)) {
            AppendLog(T(wxS("message.advancedBreakpointPersistenceError")) + error);
        }
    }

    bool SendBreakpointsForSource(const wxString& sourcePath)
    {
        if (!dap_.IsRunning() || sourcePath.empty()) return false;
        const auto requested = dapSession_.RequestedBreakpoints(sourcePath);
        std::vector<codium::DapBreakpointRequest> requests;
        requests.reserve(requested.size());
        for (const auto& breakpoint : requested) {
            requests.push_back(codium::DapBreakpointRequest{
                breakpoint.requestedLine, breakpoint.condition, breakpoint.hitCondition, breakpoint.logMessage});
        }
        if (!dap_.SetBreakpoints(sourcePath, requests)) return false;
        dapBreakpointRequests_[dap_.LastRequestSequence()] = sourcePath;
        AppendLog(T(wxS("message.dapBreakpointSent")));
        return true;
    }

    void SendAllBreakpoints()
    {
        std::set<wxString> paths;
        for (const auto& breakpoint : dapSession_.AllRequestedBreakpoints()) paths.insert(breakpoint.sourcePath);
        for (const auto& path : paths) SendBreakpointsForSource(path);
        SendFunctionBreakpoints();
        SendDataBreakpoints();
    }

    bool SendFunctionBreakpoints()
    {
        if (!dap_.IsRunning() || !supportsFunctionBreakpoints_) return false;
        std::vector<codium::DapFunctionBreakpointRequest> requests;
        requests.reserve(functionBreakpoints_.size());
        for (const auto& breakpoint : functionBreakpoints_) {
            requests.push_back(codium::DapFunctionBreakpointRequest{
                breakpoint.name, breakpoint.condition, breakpoint.hitCondition});
        }
        if (!dap_.SetFunctionBreakpoints(requests)) return false;
        dapAdvancedBreakpointRequests_[dap_.LastRequestSequence()] = DapAdvancedBreakpointRequestKind::Function;
        for (auto& breakpoint : functionBreakpoints_) {
            breakpoint.state = codium::DapBreakpointState::Pending;
            breakpoint.message.clear();
        }
        return true;
    }

    bool SendDataBreakpoints()
    {
        if (!dap_.IsRunning() || !supportsDataBreakpoints_) return false;
        std::vector<codium::DapDataBreakpointRequest> requests;
        requests.reserve(dataBreakpoints_.size());
        for (const auto& breakpoint : dataBreakpoints_) {
            requests.push_back(codium::DapDataBreakpointRequest{
                breakpoint.dataId, breakpoint.accessType, breakpoint.condition, breakpoint.hitCondition});
        }
        if (!dap_.SetDataBreakpoints(requests)) return false;
        dapAdvancedBreakpointRequests_[dap_.LastRequestSequence()] = DapAdvancedBreakpointRequestKind::Data;
        for (auto& breakpoint : dataBreakpoints_) {
            breakpoint.state = codium::DapBreakpointState::Pending;
            breakpoint.message.clear();
        }
        return true;
    }

    void ToggleBreakpoint()
    {
        ToggleBreakpointAt(CurrentEditorLine() + 1);
    }

    void ToggleBreakpointAt(int line)
    {
        if (document_.IsUntitled()) {
            AppendLog(T(wxS("dialog.openSourceBreakpoint")));
            return;
        }
        if (line <= 0) return;
        dapSession_.ToggleRequestedBreakpoint(document_.Path(), line);
        PersistBreakpoints();
        RefreshBreakpointView();
        RefreshGutters();
        SendBreakpointsForSource(document_.Path());
    }

    void ConfigureBreakpoint()
    {
        if (document_.IsUntitled()) {
            AppendLog(T(wxS("dialog.configureSourceBreakpoint")));
            return;
        }
        int line = CurrentEditorLine() + 1;
        const auto requested = dapSession_.RequestedBreakpoints(document_.Path());
        auto found = std::find_if(requested.begin(), requested.end(), [line](const codium::DapBreakpoint& breakpoint) {
            return breakpoint.requestedLine == line;
        });
        if (found == requested.end() && breakpoints_ && breakpoints_->GetSelection() != wxNOT_FOUND &&
            breakpoints_->GetSelection() < static_cast<int>(requested.size())) {
            line = requested[static_cast<size_t>(breakpoints_->GetSelection())].requestedLine;
            found = std::find_if(requested.begin(), requested.end(), [line](const codium::DapBreakpoint& breakpoint) {
                return breakpoint.requestedLine == line;
            });
        }
        if (found == requested.end()) {
            AppendLog(T(wxS("dialog.toggleBreakpointFirst")));
            return;
        }
        wxTextEntryDialog condition(this, T(wxS("dialog.conditionalExpression")), T(wxS("dialog.breakpointCondition")), found->condition);
        if (condition.ShowModal() != wxID_OK) return;
        wxTextEntryDialog hitCondition(this, T(wxS("dialog.hitCountExpression")), T(wxS("dialog.breakpointHitCondition")), found->hitCondition);
        if (hitCondition.ShowModal() != wxID_OK) return;
        wxTextEntryDialog logMessage(this, T(wxS("dialog.logMessage")), T(wxS("dialog.breakpointLogpoint")), found->logMessage);
        if (logMessage.ShowModal() != wxID_OK) return;
        dapSession_.UpdateBreakpointOptions(document_.Path(), line, condition.GetValue(),
                                             hitCondition.GetValue(), logMessage.GetValue());
        PersistBreakpoints();
        RefreshBreakpointView();
        RefreshGutters();
        SendBreakpointsForSource(document_.Path());
    }

    void ConfigureFunctionBreakpoint()
    {
        if (!dap_.IsRunning() || !supportsFunctionBreakpoints_) {
            AppendLog(wxString::Format(T(wxS("message.adapterDidNotAdvertise")), T(wxS("label.functionBreakpoints"))));
            return;
        }
        wxTextEntryDialog name(this, T(wxS("dialog.functionName")), T(wxS("dialog.functionBreakpoint")), wxEmptyString);
        if (name.ShowModal() != wxID_OK || name.GetValue().empty()) return;
        wxTextEntryDialog condition(this, T(wxS("dialog.conditionalExpression")), T(wxS("dialog.functionCondition")));
        if (condition.ShowModal() != wxID_OK) return;
        wxTextEntryDialog hit(this, T(wxS("dialog.hitCountExpression")), T(wxS("dialog.functionHitCount")));
        if (hit.ShowModal() != wxID_OK) return;
        codium::DapFunctionBreakpoint configured;
        configured.name = name.GetValue();
        configured.condition = condition.GetValue();
        configured.hitCondition = hit.GetValue();
        functionBreakpoints_.push_back(configured);
        PersistBreakpoints();
        if (SendFunctionBreakpoints()) {
            RefreshBreakpointView();
            AppendLog(T(wxS("message.dapFunctionBreakpointSent")));
        }
    }

    void ConfigureDataBreakpoint()
    {
        if (!dap_.IsRunning() || !supportsDataBreakpoints_) {
            AppendLog(wxString::Format(T(wxS("message.adapterDidNotAdvertise")), T(wxS("label.dataBreakpoints"))));
            return;
        }
        wxTextEntryDialog dataId(this, T(wxS("dialog.dataId")), T(wxS("dialog.dataBreakpoint")), wxEmptyString);
        if (dataId.ShowModal() != wxID_OK || dataId.GetValue().empty()) return;
        wxTextEntryDialog access(this, T(wxS("dialog.dataAccess")), T(wxS("dialog.dataBreakpointAccess")), wxS("write"));
        if (access.ShowModal() != wxID_OK) return;
        codium::DapDataBreakpoint configured;
        configured.dataId = dataId.GetValue();
        configured.accessType = access.GetValue();
        dataBreakpoints_.push_back(configured);
        PersistBreakpoints();
        if (SendDataBreakpoints()) {
            RefreshBreakpointView();
            AppendLog(T(wxS("message.dapDataBreakpointSent")));
        }
    }

    void RequestDebugThreads()
    {
        if (dap_.RequestThreads()) AppendLog(T(wxS("message.dapThreadsSent")));
        else AppendLog(T(wxS("dialog.startAdapterFirst")));
    }

    void RequestStackTrace()
    {
        if (dap_.RequestStackTrace(debugThreadId_)) AppendLog(T(wxS("message.dapStackTraceSent")));
        else AppendLog(T(wxS("dialog.startAdapterFirst")));
    }

    void RequestDebugScopes()
    {
        if (dap_.RequestScopes(debugFrameId_)) AppendLog(T(wxS("message.dapScopesSent")));
    }

    void RequestDebugVariables()
    {
        if (dap_.RequestVariables(debugVariablesReference_)) AppendLog(T(wxS("message.dapVariablesSent")));
    }

    void RequestDebugWatches()
    {
        if (!dap_.IsRunning() || dapSession_.State() != codium::DapRunState::Paused) return;
        for (const auto& expression : watchExpressions_) {
            if (dap_.Evaluate(expression, debugFrameId_)) {
                AppendLog(T(wxS("message.dapWatchEvaluateSent")) + expression);
            }
        }
    }

    void EvaluateDebugExpression()
    {
        wxTextEntryDialog dialog(this, T(wxS("input.expressionEvaluate")), T(wxS("dialog.debugEvaluate")), wxEmptyString);
        if (dialog.ShowModal() != wxID_OK || dialog.GetValue().empty()) return;
        if (dap_.Evaluate(dialog.GetValue(), debugFrameId_)) AppendLog(T(wxS("message.dapEvaluateSent")));
    }

    void RefreshWatchView()
    {
        if (!watches_) return;
        watches_->Clear();
        for (const auto& expression : watchExpressions_) watches_->Append(expression);
    }

    void AddWatch()
    {
        wxTextEntryDialog dialog(this, T(wxS("input.expressionWatch")), T(wxS("dialog.addWatch")), wxEmptyString);
        if (dialog.ShowModal() != wxID_OK || dialog.GetValue().empty()) return;
        if (watchExpressions_.Index(dialog.GetValue()) == wxNOT_FOUND) watchExpressions_.Add(dialog.GetValue());
        RefreshWatchView();
        wxString error;
        if (workspace_.IsOpen() && !codium::WatchStore::Save(workspace_.RootPath(), watchExpressions_, &error)) AppendLog(error);
        if (dap_.IsRunning()) {
            if (dap_.Evaluate(dialog.GetValue(), debugFrameId_)) AppendLog(T(wxS("message.dapWatchEvaluateSent")));
        }
    }

    void ConfigureSourceMap()
    {
        wxTextEntryDialog remote(this, T(wxS("dialog.remoteSourceRoot")), T(wxS("dialog.sourceMapping")), wxEmptyString);
        if (remote.ShowModal() != wxID_OK || remote.GetValue().empty()) return;
        wxTextEntryDialog local(this, T(wxS("dialog.localSourceRoot")), T(wxS("dialog.sourceMapping")), WorkspaceDirectory());
        if (local.ShowModal() != wxID_OK || local.GetValue().empty()) return;
        sourceMapper_.Add(remote.GetValue(), local.GetValue());
        AppendLog(T(wxS("message.sourceMappingAdded")) + remote.GetValue() + wxS(" -> ") + local.GetValue());
    }

    wxString SourceMapArguments() const
    {
        return sourceMapper_.ToJson();
    }

    void ContinueDebug()
    {
        if (dap_.SendRequest(wxS("continue"), wxS("{\"threadId\":1}"))) AppendLog(T(wxS("message.dapContinueSent")));
    }

    void PauseDebug()
    {
        if (dap_.SendRequest(wxS("pause"), wxS("{\"threadId\":1}"))) AppendLog(T(wxS("message.dapPauseSent")));
    }

    void StopDebug()
    {
        if (dap_.IsRunning()) {
            dap_.Stop();
            dapAdvancedBreakpointRequests_.clear();
            dapSession_.MarkDisconnected();
            UpdateDapStatus();
            AppendLog(T(wxS("message.debugAdapterStopped")));
        }
    }

    void OnDebugFinished(wxProcessEvent& event)
    {
        dap_.HandleProcessExit(event.GetPid(), event.GetExitCode());
        dapAdvancedBreakpointRequests_.clear();
        dapSession_.MarkDisconnected();
        UpdateDapStatus();
        AppendLog(wxString::Format(T(wxS("message.debugAdapterFinished")), event.GetExitCode()));
    }

    bool EnsureEditorAvailable()
    {
        if (editor_) return true;
        AppendLog(T(wxS("dialog.openEditorFirst")));
        return false;
    }

    void SelectEditorMatch(const codium::EditorMatch& match)
    {
        if (!editor_ || !match.Found()) return;
        editor_->SetSelection(match.start, match.start + match.length);
        editor_->ShowPosition(match.start);
        editor_->SetFocus();
        UpdateTitle();
    }

    void FindInEditor(bool backwards)
    {
        if (!EnsureEditorAvailable()) return;
        wxString initial = lastSearchQuery_;
        long selectionFrom = 0;
        long selectionTo = 0;
        editor_->GetSelection(&selectionFrom, &selectionTo);
        if (selectionFrom != selectionTo) {
            initial = editor_->GetStringSelection();
        }
        wxTextEntryDialog dialog(this, T(wxS("input.findText")), T(wxS("dialog.find")), initial);
        if (dialog.ShowModal() != wxID_OK || dialog.GetValue().empty()) return;
        lastSearchQuery_ = dialog.GetValue();
        FindNextInEditor(backwards);
    }

    void FindNextInEditor(bool backwards)
    {
        if (!EnsureEditorAvailable()) return;
        if (lastSearchQuery_.empty()) {
            FindInEditor(backwards);
            return;
        }
        const wxString text = editor_->GetValue();
        long selectionFrom = 0;
        long selectionTo = 0;
        editor_->GetSelection(&selectionFrom, &selectionTo);
        const long start = backwards
            ? std::max(0L, selectionFrom - 1) : selectionTo;
        codium::EditorMatch match = codium::EditorActions::Find(
            text, lastSearchQuery_, start, backwards, lastSearchMatchCase_);
        if (!match.Found()) {
            match = codium::EditorActions::Find(
                text, lastSearchQuery_, backwards ? text.length() : 0, backwards, lastSearchMatchCase_);
        }
        if (!match.Found()) {
            AppendLog(T(wxS("message.searchNotFound")) + lastSearchQuery_);
            return;
        }
        SelectEditorMatch(match);
    }

    void ReplaceInEditor()
    {
        if (!EnsureEditorAvailable()) return;
        wxTextEntryDialog findDialog(this, T(wxS("input.findText")), T(wxS("dialog.replace")), lastSearchQuery_);
        if (findDialog.ShowModal() != wxID_OK || findDialog.GetValue().empty()) return;
        lastSearchQuery_ = findDialog.GetValue();
        wxTextEntryDialog replacementDialog(this, T(wxS("input.replaceWith")), T(wxS("dialog.replace")), lastReplacement_);
        if (replacementDialog.ShowModal() != wxID_OK) return;
        lastReplacement_ = replacementDialog.GetValue();

        const wxString text = editor_->GetValue();
        long selectionFrom = 0;
        long selectionTo = 0;
        editor_->GetSelection(&selectionFrom, &selectionTo);
        const long start = selectionFrom != selectionTo ? selectionFrom : editor_->GetInsertionPoint();
        auto match = codium::EditorActions::Find(text, lastSearchQuery_, start, false, lastSearchMatchCase_);
        if (!match.Found()) match = codium::EditorActions::Find(text, lastSearchQuery_, 0, false, lastSearchMatchCase_);
        if (!match.Found()) {
            AppendLog(T(wxS("message.searchNotFound")) + lastSearchQuery_);
            return;
        }
        editor_->Replace(match.start, match.start + match.length, lastReplacement_);
        editor_->SetSelection(match.start, match.start + lastReplacement_.length());
        editor_->ShowPosition(match.start);
        editor_->SetFocus();
        AppendLog(wxString::Format(T(wxS("message.replacedOne")), lastSearchQuery_));
        UpdateTitle();
    }

    void ReplaceAllInEditor()
    {
        if (!EnsureEditorAvailable()) return;
        wxTextEntryDialog findDialog(this, T(wxS("input.findText")), T(wxS("dialog.replaceAll")), lastSearchQuery_);
        if (findDialog.ShowModal() != wxID_OK || findDialog.GetValue().empty()) return;
        lastSearchQuery_ = findDialog.GetValue();
        wxTextEntryDialog replacementDialog(this, T(wxS("input.replaceWith")), T(wxS("dialog.replaceAll")), lastReplacement_);
        if (replacementDialog.ShowModal() != wxID_OK) return;
        lastReplacement_ = replacementDialog.GetValue();

        int replacements = 0;
        const wxString replaced = codium::EditorActions::ReplaceAll(
            editor_->GetValue(), lastSearchQuery_, lastReplacement_, lastSearchMatchCase_, &replacements);
        if (replacements == 0) {
            AppendLog(T(wxS("message.searchNotFound")) + lastSearchQuery_);
            return;
        }
        editor_->SetValue(replaced);
        editor_->SetInsertionPointEnd();
        editor_->SetFocus();
        AppendLog(wxString::Format(T(wxS("message.replacedAll")), replacements, lastSearchQuery_));
        UpdateTitle();
    }

    void GoToLine()
    {
        if (!EnsureEditorAvailable()) return;
        const int currentLine = codium::EditorActions::LineColumnForPosition(
            editor_->GetValue(), editor_->GetInsertionPoint()).line + 1;
        wxTextEntryDialog dialog(this, T(wxS("input.lineNumber")), T(wxS("dialog.goToLine")),
                                 wxString::Format(wxS("%d"), currentLine));
        if (dialog.ShowModal() != wxID_OK) return;
        long line = 0;
        if (!dialog.GetValue().ToLong(&line) || line < 1) {
            AppendLog(T(wxS("dialog.goToLinePositive")));
            return;
        }
        const long position = codium::EditorActions::PositionForLineColumn(
            editor_->GetValue(), static_cast<int>(line - 1), 0);
        editor_->SetInsertionPoint(position);
        editor_->ShowPosition(position);
        editor_->SetFocus();
        UpdateTitle();
    }

    void GoToFile()
    {
        if (!workspace_.IsOpen()) {
            AppendLog(T(wxS("dialog.noWorkspaceFile")));
            return;
        }
        wxTextEntryDialog queryDialog(this, T(wxS("input.filePath")), T(wxS("dialog.goToFile")), wxEmptyString);
        if (queryDialog.ShowModal() != wxID_OK) return;
        const wxString query = queryDialog.GetValue().Lower();
        if (query.empty()) return;
        wxArrayString choices;
        wxArrayString paths;
        for (const auto& path : workspace_.Files()) {
            const wxString relative = workspace_.RelativePath(path);
            if (relative.Lower().Find(query) == wxNOT_FOUND) continue;
            choices.Add(relative);
            paths.Add(path);
        }
        if (choices.IsEmpty()) {
            AppendLog(T(wxS("dialog.noFileMatch")));
            return;
        }
        wxSingleChoiceDialog choice(this, T(wxS("dialog.selectFile")), T(wxS("dialog.goToFile")), choices);
        if (choice.ShowModal() == wxID_OK) OpenDocumentPath(paths[choice.GetSelection()]);
    }

    bool EnsureLanguageRequest(const wxString& action)
    {
        if (document_.IsUntitled()) {
            AppendLog(T(wxS("dialog.openDocumentFirst")) + action + wxS("."));
            return false;
        }
        if (!languageServerInitialized_) {
            AppendLog(T(wxS("dialog.initializeFirst")) + action + wxS("."));
            return false;
        }
        return true;
    }

    void RequestDefinition()
    {
        if (EnsureLanguageRequest(wxS("definition")) &&
            host_.RequestLanguageDefinition(DocumentUri(), CurrentEditorLine(), CurrentEditorCharacter())) {
            lspResultMode_ = LspResultMode::Locations;
            AppendLog(T(wxS("message.lspDefinitionSent")));
        }
    }

    void RequestDeclaration()
    {
        if (EnsureLanguageRequest(wxS("declaration")) &&
            host_.RequestLanguageDeclaration(DocumentUri(), CurrentEditorLine(), CurrentEditorCharacter())) {
            lspResultMode_ = LspResultMode::Locations;
            AppendLog(T(wxS("message.lspDeclarationSent")));
        }
    }

    void RequestReferences()
    {
        if (EnsureLanguageRequest(wxS("references")) &&
            host_.RequestLanguageReferences(DocumentUri(), CurrentEditorLine(), CurrentEditorCharacter())) {
            lspResultMode_ = LspResultMode::Locations;
            AppendLog(T(wxS("message.lspReferencesSent")));
        }
    }

    void RequestDocumentSymbols()
    {
        if (EnsureLanguageRequest(wxS("document symbols")) && host_.RequestLanguageDocumentSymbols(DocumentUri())) {
            lspResultMode_ = LspResultMode::Locations;
            AppendLog(T(wxS("message.lspDocumentSymbolsSent")));
        }
    }

    void RequestWorkspaceSymbols()
    {
        if (!EnsureLanguageRequest(wxS("workspace symbols"))) return;
        wxTextEntryDialog dialog(this, T(wxS("input.symbolQuery")), T(wxS("dialog.workspaceSymbols")), lastSymbolQuery_);
        if (dialog.ShowModal() != wxID_OK) return;
        lastSymbolQuery_ = dialog.GetValue();
        if (host_.RequestLanguageWorkspaceSymbols(lastSymbolQuery_)) {
            lspResultMode_ = LspResultMode::Locations;
            AppendLog(T(wxS("message.lspWorkspaceSymbolsSent")));
        }
    }

    void RenameSymbol()
    {
        if (!EnsureLanguageRequest(wxS("rename"))) return;
        wxTextEntryDialog dialog(this, T(wxS("input.newSymbolName")), T(wxS("dialog.renameSymbol")));
        if (dialog.ShowModal() != wxID_OK || dialog.GetValue().empty()) return;
        if (host_.RequestLanguageRename(DocumentUri(), CurrentEditorLine(), CurrentEditorCharacter(), dialog.GetValue())) {
            AppendLog(T(wxS("message.lspRenameSent")));
        }
    }

    void RequestCodeActions()
    {
        if (EnsureLanguageRequest(wxS("code actions")) &&
            host_.RequestLanguageCodeActions(DocumentUri(), CurrentEditorLine(), CurrentEditorCharacter())) {
            lspResultMode_ = LspResultMode::CodeActions;
            AppendLog(T(wxS("message.lspCodeActionsSent")));
        }
    }

    void ShowCommandPalette()
    {
        const wxArrayString commands = {
            T(wxS("menu.openWorkspace")), T(wxS("menu.openFile")), T(wxS("menu.saveFile")),
            T(wxS("menu.startClangd")), T(wxS("menu.initializeLsp")),
            T(wxS("menu.requestHover")), T(wxS("menu.requestCompletion")),
            T(wxS("menu.buildProject")), T(wxS("menu.rerunBuild")), T(wxS("menu.runTask")), T(wxS("menu.stopTask")),
            T(wxS("menu.startTerminal")), T(wxS("menu.sendTerminal")), T(wxS("menu.stopTerminal")), T(wxS("menu.selectShell")),
            T(wxS("menu.startDebug")), T(wxS("menu.initializeDebug")), T(wxS("menu.continue")),
            T(wxS("menu.launchProgram")), T(wxS("menu.pause")), T(wxS("menu.stopDebug")),
            T(wxS("menu.startHost")), T(wxS("menu.loadDemo")),
            T(wxS("menu.runDemo")), T(wxS("menu.stopLsp")),
            T(wxS("menu.installVsix")), T(wxS("menu.listExtensions")), T(wxS("menu.discoverCodeBlocks")),
            T(wxS("menu.startCodeBlocksAdapter")), T(wxS("menu.stopCodeBlocksAdapter")),
            T(wxS("menu.previousProblem")), T(wxS("menu.nextProblem")),
            T(wxS("button.cbDebug")), T(wxS("button.cbContinue")),
            T(wxS("button.cbPause")), T(wxS("button.cbStop")), T(wxS("button.buildAndRun")),
            T(wxS("menu.find")), T(wxS("menu.findNext")), T(wxS("menu.findPrevious")), T(wxS("menu.replace")),
            T(wxS("menu.replaceAll")), T(wxS("menu.goToLine")),
            T(wxS("menu.goToFile")), T(wxS("menu.goToSymbol")), T(wxS("menu.goToDefinition")),
            T(wxS("menu.goToDeclaration")), T(wxS("menu.findReferences")), T(wxS("menu.renameSymbol")),
            T(wxS("menu.codeActions")), T(wxS("dialog.workspaceSymbols"))
        };
        wxSingleChoiceDialog dialog(this, T(wxS("dialog.selectCommand")), T(wxS("dialog.commandPalette")), commands);
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
        case 32: StartCodeBlocksDebug(); break;
        case 33: ContinueCodeBlocksDebug(); break;
        case 34: PauseCodeBlocksDebug(); break;
        case 35: StopCodeBlocksDebug(); break;
        case 36: RunSelectedTarget(); break;
        case 37: FindInEditor(false); break;
        case 38: FindNextInEditor(false); break;
        case 39: FindNextInEditor(true); break;
        case 40: ReplaceInEditor(); break;
        case 41: ReplaceAllInEditor(); break;
        case 42: GoToLine(); break;
        case 43: GoToFile(); break;
        case 44: RequestDocumentSymbols(); break;
        case 45: RequestDefinition(); break;
        case 46: RequestDeclaration(); break;
        case 47: RequestReferences(); break;
        case 48: RenameSymbol(); break;
        case 49: RequestCodeActions(); break;
        case 50: RequestWorkspaceSymbols(); break;
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
            AppendLog(T(wxS("message.errorPrefix")) + error);
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
        if (!customEditor.empty()) AppendLog(T(wxS("message.customEditorSelected")) + customEditor);
        AppendLog(T(wxS("message.documentOpened")) + path);
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
            ApplyInlineProblems();
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
        semanticTokens_.clear();
        UpdateTitle();
        RefreshBreakpointView();
        RefreshGutters();
        ApplyInlineProblems();
        NotifyLanguageDocumentOpened();
    }

    void SwitchAdjacentTab(int direction)
    {
        if (!notebook_ || tabPaths_.empty()) return;
        const int current = notebook_->GetSelection();
        if (current == wxNOT_FOUND) return;
        const int count = static_cast<int>(tabPaths_.size());
        const int next = (current + direction + count) % count;
        notebook_->SetSelection(next);
        SwitchToTab(static_cast<size_t>(next));
    }

    void SaveFile()
    {
        if (document_.IsUntitled()) {
            wxFileDialog dialog(this, T(wxS("dialog.saveSourceFile")), wxEmptyString, wxEmptyString,
                                T(wxS("dialog.allFilesFilter")), wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
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
            ApplySyntaxHighlighting();
        }

        document_.SetText(editor_->GetValue());
        wxString error;
        if (!document_.Save(&error)) {
            AppendLog(T(wxS("message.errorPrefix")) + error);
            return;
        }
        document_.MarkClean();
        UpdateTitle();
        AppendLog(T(wxS("message.documentSaved")) + document_.Path());
        NotifyLanguageDocumentChanged();
        if (host_.IsRunning() && !document_.IsUntitled()) {
            host_.NotifyDocumentSaved(DocumentUri(), languageId_, documentVersion_, document_.Text());
        }
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
            AppendLog(T(wxS("message.hostAlreadyRunning")));
            return;
        }
        if (host_.Start(HostScript())) {
            AppendLog(T(wxS("message.hostStarted")));
        } else {
            AppendLog(T(wxS("message.hostStartError")));
        }
    }

    void LoadDemo()
    {
        if (!host_.IsRunning()) {
            AppendLog(T(wxS("dialog.startHostFirst")));
            return;
        }
        if (host_.LoadExtension(DemoExtension())) {
            AppendLog(T(wxS("message.loadDemoSent")));
        }
    }

    void ExecuteDemo()
    {
        if (!host_.IsRunning()) {
            AppendLog(T(wxS("dialog.startHostFirst")));
            return;
        }
        if (host_.ExecuteCommand(wxS("hello.codium"))) {
            AppendLog(T(wxS("message.runDemoSent")));
        }
    }

    void StartLanguageServer()
    {
        if (!host_.IsRunning()) {
            StartHost();
        }
        semanticTokensSupported_ = false;
        semanticTokens_.clear();
        if (host_.StartLanguageServer(wxS("clangd"))) {
            AppendLog(T(wxS("message.startClangdSent")));
        }
    }

    void InitializeLanguageServer()
    {
        if (!host_.IsRunning()) {
            StartHost();
        }
        if (host_.InitializeLanguageServer(ProjectRootUri())) {
            languageServerInitialized_ = true;
            AppendLog(T(wxS("message.initializeLanguageServerSent")));
            NotifyLanguageDocumentOpened();
        }
    }

    void NotifyLanguageDocumentOpened()
    {
        if (document_.IsUntitled()) {
            return;
        }
        if (host_.IsRunning()) {
            host_.NotifyDocumentOpened(DocumentUri(), languageId_, documentVersion_, document_.Text());
        }
        if (!languageServerInitialized_) return;
        host_.OpenLanguageDocument(DocumentUri(), languageId_, documentVersion_, document_.Text());
        semanticTokens_.clear();
        if (semanticTokensSupported_) host_.RequestLanguageSemanticTokens(DocumentUri());
    }

    void NotifyLanguageDocumentChanged()
    {
        if (document_.IsUntitled()) {
            return;
        }
        if (host_.IsRunning()) {
            host_.NotifyDocumentChanged(DocumentUri(), languageId_, documentVersion_, document_.Text());
        }
        if (!languageServerInitialized_) return;
        host_.ChangeLanguageDocument(DocumentUri(), documentVersion_, document_.Text());
        semanticTokens_.clear();
        if (semanticTokensSupported_) host_.RequestLanguageSemanticTokens(DocumentUri());
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
            AppendLog(T(wxS("dialog.openDocumentHover")));
            return;
        }
        if (host_.RequestLanguageHover(DocumentUri(), CurrentEditorLine(), CurrentEditorCharacter())) {
            AppendLog(T(wxS("message.lspHoverSent")));
        }
    }

    void RequestCompletion()
    {
        if (document_.IsUntitled()) {
            AppendLog(T(wxS("dialog.openDocumentCompletion")));
            return;
        }
        if (host_.RequestLanguageCompletion(DocumentUri(), CurrentEditorLine(), CurrentEditorCharacter())) {
            AppendLog(T(wxS("message.lspCompletionSent")));
        }
    }

    void RequestSemanticTokens()
    {
        if (document_.IsUntitled()) {
            AppendLog(T(wxS("dialog.openDocumentSemantic")));
            return;
        }
        if (!languageServerInitialized_) {
            AppendLog(T(wxS("message.lspInitializeFirstSemantic")));
            return;
        }
        if (host_.RequestLanguageSemanticTokens(DocumentUri())) {
            AppendLog(T(wxS("message.lspSemanticTokensSent")));
        }
    }

    void StopLanguageServer()
    {
        semanticTokensSupported_ = false;
        semanticTokens_.clear();
        if (host_.StopLanguageServer()) {
            AppendLog(T(wxS("message.stopLanguageServerSent")));
        }
    }

    void InstallVsix()
    {
        if (!EnsureWorkspaceTrusted()) return;
        wxFileDialog dialog(this, T(wxS("dialog.chooseVsix")), wxEmptyString, wxEmptyString,
                            T(wxS("dialog.vsixFilter")), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK) {
            return;
        }

        wxString message;
        if (extensions_.Install(dialog.GetPath(), &message)) {
            AppendLog(message);
        } else {
            AppendLog(T(wxS("message.errorPrefix")) + message);
        }
    }

    void ListExtensions()
    {
        const wxArrayString installed = extensions_.ListInstalled();
        if (installed.IsEmpty()) {
            AppendLog(T(wxS("dialog.noVsix")));
            return;
        }
        for (const auto& name : installed) {
            AppendLog(T(wxS("message.extensionPrefix")) + name);
        }
    }

    void DiscoverCodeBlocks()
    {
        if (bottomWorkbench_) bottomWorkbench_->SetSelection(4);
        wxString error;
        if (!codeBlocksBridge_.Discover(&error)) {
            AppendLog(T(wxS("message.codeBlocksDiscovery")) + error);
            wxDirDialog dialog(this, T(wxS("dialog.chooseCodeBlocksRoot")), wxEmptyString,
                               wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
            if (dialog.ShowModal() != wxID_OK) return;
            codeBlocksBridge_ = codium::CodeBlocksBridge(dialog.GetPath());
            if (!codeBlocksBridge_.Discover(&error)) {
                AppendLog(T(wxS("message.codeBlocksDiscovery")) + error);
                return;
            }
        }
        AppendLog(wxString::Format(T(wxS("message.codeBlocksRoot")), codeBlocksBridge_.Root()));
        if (!codeBlocksBridge_.SdkIncludeDirectory().empty()) {
            AppendLog(T(wxS("message.codeBlocksHeaders")) + codeBlocksBridge_.SdkIncludeDirectory());
        }
        if (codeBlocksBridge_.Plugins().empty()) {
            AppendLog(T(wxS("message.noCodeBlocksPlugins")));
        } else {
            for (const auto& plugin : codeBlocksBridge_.Plugins()) {
                AppendLog(wxString::Format(T(wxS("message.codeBlocksPlugin")),
                                           plugin.title, plugin.filePath,
                                           plugin.manifestValid ? plugin.status : T(wxS("label.manifestUnavailable"))));
            }
        }
        AppendLog(T(wxS("message.codeBlocksLoadPolicy")));
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
            AppendLog(T(wxS("dialog.openWorkspaceAdapter")));
            return;
        }
        wxString executable;
        wxGetEnv(wxS("CODIUM_BLOCKS_CODEBLOCKS_ADAPTER"), &executable);
        if (executable.empty()) {
            wxFileDialog dialog(this, T(wxS("dialog.chooseAdapterExecutable")), wxEmptyString, wxEmptyString,
                                T(wxS("dialog.executableFilter")), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
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
        const wxString debuggerProvider = FindCodeBlocksDebuggerProvider(executable);
        if (dataDirectory.empty() || compilerPlugin.empty()) {
            AppendLog(T(wxS("message.codeBlocksRuntimeIncomplete")));
            return;
        }
        arguments.Add(wxS("--data-dir=") + dataDirectory);
        arguments.Add(wxS("--compiler-plugin=") + compilerPlugin);
        if (!debuggerPlugin.empty()) arguments.Add(wxS("--debugger-plugin=") + debuggerPlugin);
        if (!debuggerProvider.empty()) arguments.Add(wxS("--debugger-provider=") + debuggerProvider);
        if (!codeBlocksAdapter_.Start(executable, arguments, workspace_.RootPath(), configuration, &error)) {
            AppendLog(T(wxS("message.codeBlocksAdapterError")) + error);
            return;
        }
        adapterProjectOpened_ = false;
        bottomWorkbench_->SetSelection(4);
        AppendLog(T(wxS("message.codeBlocksAdapterStarted")));
    }

    void StopCodeBlocksAdapter()
    {
        if (codeBlocksAdapter_.IsRunning()) {
            codeBlocksAdapter_.Stop();
            AppendLog(T(wxS("message.codeBlocksAdapterStopped")));
        }
    }

    bool CodeBlocksCapability(const wxString& capability) const
    {
        return codeBlocksAdapter_.Capabilities().Index(capability) != wxNOT_FOUND;
    }

    wxString CodeBlocksTargetSelection() const
    {
        if (targetChoice_ && targetChoice_->GetSelection() != wxNOT_FOUND) {
            return targetChoice_->GetStringSelection();
        }
        return wxS("Debug");
    }

    void StartCodeBlocksDebug()
    {
        if (!EnsureWorkspaceTrusted()) return;
        if (!codeBlocksAdapter_.IsRunning()) StartCodeBlocksAdapter();
        if (!codeBlocksAdapter_.IsReady()) {
            AppendLog(T(wxS("dialog.waitAdapterHandshake")));
            return;
        }
        const wxString projectFile = CodeBlocksProjectFile();
        if (projectFile.empty()) {
            AppendLog(T(wxS("dialog.requireCodeBlocksProject")));
            return;
        }
        if (!adapterProjectOpened_ && !codeBlocksAdapter_.OpenProject(projectFile)) {
            AppendLog(T(wxS("message.codeBlocksProjectOpenError")));
            return;
        }
        adapterProjectOpened_ = true;
        if (!codeBlocksAdapter_.DebugProject(projectFile, CodeBlocksTargetSelection(), true)) {
            AppendLog(T(wxS("message.codeBlocksDebugStartError")));
            return;
        }
        AppendLog(T(wxS("message.codeBlocksDebugLaunchSent")));
    }

    void ContinueCodeBlocksDebug()
    {
        if (codeBlocksAdapter_.ContinueDebug()) AppendLog(T(wxS("message.codeBlocksDebugContinueSent")));
        else AppendLog(T(wxS("dialog.codeBlocksDebuggerNotReady")));
    }

    void PauseCodeBlocksDebug()
    {
        if (codeBlocksAdapter_.PauseDebug()) AppendLog(T(wxS("message.codeBlocksDebugPauseSent")));
        else AppendLog(T(wxS("dialog.codeBlocksDebuggerNotReady")));
    }

    void StopCodeBlocksDebug()
    {
        if (codeBlocksAdapter_.StopDebug()) AppendLog(T(wxS("message.codeBlocksDebugStopSent")));
        else AppendLog(T(wxS("dialog.codeBlocksDebuggerNotReady")));
    }

    wxString FormatCodeBlocksValue(const codium::CodeBlocksDebugValue& value, int depth = 0) const
    {
        wxString label = value.symbol.empty() ? value.full : value.symbol;
        if (label.empty()) label = T(wxS("label.unknownValue"));
        wxString result(static_cast<size_t>(std::max(0, depth)) * 2, wxChar(' '));
        result += label;
        if (!value.type.empty()) result += wxS(" : ") + value.type;
        if (!value.value.empty()) result += wxS(" = ") + value.value;
        if (value.valueError) result += T(wxS("label.valueError"));
        if (value.changed) result += T(wxS("label.valueChanged"));
        if (value.truncated) result += T(wxS("label.valueTruncated"));
        for (const auto& child : value.children) result += wxS("\n") + FormatCodeBlocksValue(child, depth + 1);
        return result;
    }

    void RequestCodeBlocksDebuggerSnapshots()
    {
        if (!codeBlocksAdapter_.IsReady()) return;
        codeBlocksAdapter_.RequestDebugSnapshot(wxS("state"));
        if (CodeBlocksCapability(wxS("debuggerStackFrames"))) {
            codeBlocksAdapter_.RequestDebugSnapshot(wxS("frames"));
        }
        if (CodeBlocksCapability(wxS("debuggerThreads"))) {
            codeBlocksAdapter_.RequestDebugSnapshot(wxS("threads"));
        }
        if (CodeBlocksCapability(wxS("debuggerBreakpoints"))) {
            codeBlocksAdapter_.RequestDebugSnapshot(wxS("breakpoints"));
        }
        if (!watchExpressions_.empty() && CodeBlocksCapability(wxS("debuggerWatches"))) {
            codeBlocksAdapter_.RequestDebugSnapshot(wxS("watches"), watchExpressions_[0]);
        }
        if (!watchExpressions_.empty() && CodeBlocksCapability(wxS("debuggerVariables"))) {
            codeBlocksAdapter_.RequestDebugSnapshot(wxS("variables"), watchExpressions_[0]);
        }
    }

    void HandleCodeBlocksSnapshot(const codium::CodeBlocksHostEvent& event)
    {
        const wxString json = event.snapshotJson.empty() ? event.payload : event.snapshotJson;
        codium::CodeBlocksDebugSnapshot snapshot;
        wxString error;
        if (!codium::CodeBlocksDebugSnapshot::Parse(json, &snapshot, &error)) {
            AppendLog(T(wxS("message.codeBlocksSnapshotParseError")) + error);
            if (debugConsole_) debugConsole_->AppendText(T(wxS("message.snapshotParseError")) + error + wxS("\n"));
            return;
        }

        if (snapshot.dataKind == wxS("frames")) {
            debugFrameLocations_.clear();
            if (callStack_) callStack_->Clear();
            for (const auto& frame : snapshot.frames) {
                DebugFrameLocation location;
                location.id = frame.number;
                location.line = frame.hasLine ? frame.line : 0;
                location.character = 0;
                location.path = sourceMapper_.Map(frame.file);
                debugFrameLocations_.push_back(location);
                if (callStack_) {
                    wxString label = wxString::Format(wxS("#%d %s"), frame.number,
                                                      frame.function.empty() ? T(wxS("label.unknownFrame")) : frame.function);
                    if (!frame.file.empty()) {
                        label += wxString::Format(wxS(" — %s"), location.path.empty() ? frame.file : location.path);
                    }
                    if (frame.hasLine) label += wxString::Format(wxS(":%d"), frame.line);
                    callStack_->Append(label);
                }
            }
            if (callStack_ && callStack_->IsEmpty()) callStack_->Append(T(wxS("label.noStackFrames")));
            if (snapshot.activeFrame >= 0 && static_cast<size_t>(snapshot.activeFrame) < callStack_->GetCount()) {
                callStack_->SetSelection(snapshot.activeFrame);
            }
        } else if (snapshot.dataKind == wxS("threads")) {
            if (debugThreads_) {
                debugThreads_->Clear();
                for (const auto& thread : snapshot.threads) {
                    debugThreads_->Append(wxString::Format(wxS("%s #%d — %s"),
                                                           thread.active ? wxS("●") : wxS("○"),
                                                           thread.number,
                                                           thread.info.empty() ? T(wxS("label.unnamedThread")) : thread.info));
                }
                if (debugThreads_->IsEmpty()) debugThreads_->Append(T(wxS("label.noThreads")));
            }
        } else if (snapshot.dataKind == wxS("breakpoints")) {
            if (breakpoints_) {
                breakpoints_->Clear();
                for (const auto& breakpoint : snapshot.breakpoints) {
                    wxString label = breakpoint.location;
                    if (label.empty()) label = wxString::Format(T(wxS("label.lineNumber")), breakpoint.line);
                    label += breakpoint.enabled ? T(wxS("label.enabled")) : T(wxS("label.disabled"));
                    if (breakpoint.temporary) label += T(wxS("label.temporary"));
                    if (!breakpoint.info.empty()) label += wxS(" — ") + breakpoint.info;
                    breakpoints_->Append(label);
                }
                if (breakpoints_->IsEmpty()) breakpoints_->Append(T(wxS("label.noCodeBlocksBreakpoints")));
            }
        } else if (snapshot.dataKind == wxS("watches")) {
            if (watches_) {
                watches_->Clear();
                if (snapshot.hasValue) watches_->Append(FormatCodeBlocksValue(snapshot.value));
                else watches_->Append(T(wxS("label.noWatchValue")));
            }
        } else if (snapshot.dataKind == wxS("variables")) {
            if (variables_) {
                variables_->Clear();
                if (snapshot.hasValue) variables_->Append(FormatCodeBlocksValue(snapshot.value));
                else variables_->Append(T(wxS("label.noVariableValue")));
            }
        }

        if (debugConsole_) {
            debugConsole_->AppendText(wxString::Format(T(wxS("message.codeBlocksSnapshotApplied")),
                                                       snapshot.dataKind.empty() ? event.dataKind : snapshot.dataKind));
        }
    }

    void HandleCodeBlocksEvent(const codium::CodeBlocksHostEvent& event)
    {
        const wxString source = wxS("Code::Blocks adapter");
        switch (event.kind) {
        case codium::CodeBlocksEventKind::CompilerDiagnostic: {
            codium::Problem problem;
            problem.source = source;
            problem.buildSessionId = activeBuildSessionId_;
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
            if (buildOutput_) buildOutput_->AppendText(wxS("\n") +
                wxString::Format(T(wxS("message.codeBlocksBuildHeader")), event.target) + wxS("\n"));
            SetStatusText(T(wxS("status.codeBlocksBuilding")), 1);
            break;
        case codium::CodeBlocksEventKind::BuildFinished:
            if (buildOutput_) buildOutput_->AppendText(event.message + wxS("\n"));
            AppendBuildSessionOutput(event.message);
            FinishBuildSession(event.exitCode);
            SetStatusText(event.exitCode == 0 ? T(wxS("status.codeBlocksBuildSucceeded")) : T(wxS("status.codeBlocksBuildFailed")), 1);
            if (pendingRunAfterBuild_) {
                if (event.exitCode == 0) LaunchPendingRun();
                else {
                    pendingRunAfterBuild_ = false;
                    pendingRunTaskName_.clear();
                    pendingRunConfiguration_.clear();
                    pendingRunArtifactOverride_.clear();
                    AppendLog(T(wxS("message.codeBlocksBuildRunSkipped")));
                }
            }
            break;
        case codium::CodeBlocksEventKind::CompilerOutput: {
            if (buildOutput_) buildOutput_->AppendText(event.message + wxS("\n"));
            AppendBuildSessionOutput(event.message);
            const wxString problemLine = event.isError ? T(wxS("label.stderrPrefix")) + event.message : event.message;
            const wxString root = event.projectPath.empty()
                ? WorkspaceDirectory()
                : wxFileName(event.projectPath).GetPath();
            problemStore_.AddCompilerLine(problemLine, source, root, activeBuildSessionId_);
            break;
        }
        case codium::CodeBlocksEventKind::DebugSnapshot:
            HandleCodeBlocksSnapshot(event);
            break;
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
            if (debugConsole_ && (event.kind == codium::CodeBlocksEventKind::DebugSessionStarted ||
                                  event.kind == codium::CodeBlocksEventKind::DebugSessionStopped)) {
                debugConsole_->AppendText(event.message + wxS("\n"));
            }
            break;
        case codium::CodeBlocksEventKind::DebugSessionPaused:
            RequestCodeBlocksDebuggerSnapshots();
            if (debugConsole_) debugConsole_->AppendText(event.message + wxS("\n"));
            break;
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
        AppendLog(wxString::Format(T(wxS("message.adapterEvent")), codium::CodeBlocksEventKindName(event.kind), event.message));
    }

    void SearchOpenVsx()
    {
        if (!EnsureWorkspaceTrusted()) return;
        wxTextEntryDialog dialog(this, T(wxS("dialog.searchOpenVsx")), T(wxS("dialog.extensionRegistry")), wxEmptyString);
        if (dialog.ShowModal() != wxID_OK || dialog.GetValue().empty()) return;
        wxString catalog;
        wxString error;
        if (extensionRegistry_.SearchOpenVsx(wxS("https://open-vsx.org"), dialog.GetValue(), &catalog, &error)) {
            AppendLog(wxString::Format(T(wxS("message.openVsxCatalogReceived")),
                                       static_cast<unsigned long>(catalog.length())));
        } else {
            AppendLog(T(wxS("message.openVsxError")) + error);
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
                AppendLog(T(wxS("message.runContributedCommand")) + command + wxS("."));
            }
        });
        extensionControls_->Layout();
        Layout();
        AppendLog(T(wxS("message.registeredExtensionCommand")) + command);
    }

    void RegisterContributedView(const wxString& line)
    {
        const wxString viewId = JsonStringField(line, wxS("viewId"));
        const wxString title = JsonStringField(line, wxS("title"));
        if (viewId.empty() || title.empty()) return;
        treeRegistry_.Register(viewId, title);
        RefreshNativeContributions();
        AppendLog(T(wxS("message.registeredTreeView")) + title);
    }

    void UpdateContributedView(const wxString& line)
    {
        const wxString viewId = JsonStringField(line, wxS("viewId"));
        if (viewId.empty()) return;
        treeRegistry_.SetItems(viewId, JsonStringFields(line, wxS("label")));
        RefreshNativeContributions();
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
            AppendLog(T(wxS("dialog.noVisibleProblems")));
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

    void UpdateDapStatus()
    {
        const wxString state = LocalizedDapRunState(dapSession_.State());
        wxString label = T(wxS("label.dapPrefix")) + state;
        if (activeDebugFrameLine_ > 0) {
            label += wxString::Format(wxS(" · frame %d:%d"), activeDebugFrameLine_, activeDebugFrameCharacter_);
        }
        if (debugStatus_) debugStatus_->SetLabel(label);
        if (GetStatusBar()) SetStatusText(T(wxS("status.debugPrefix")) + label, 1);
    }

    void ClearDapTransientViews()
    {
        debugFrameLocations_.clear();
        activeDebugFramePath_.clear();
        activeDebugFrameLine_ = 0;
        activeDebugFrameCharacter_ = 0;
        debugThreadId_ = 1;
        debugFrameId_ = 1;
        debugVariablesReference_ = 0;
        if (debugThreads_) {
            debugThreads_->Clear();
            debugThreads_->Append(T(wxS("label.noThreads")));
        }
        if (callStack_) {
            callStack_->Clear();
            callStack_->Append(T(wxS("label.noStackFrames")));
        }
        if (variables_) variables_->Clear();
        RefreshWatchView();
    }

    void ApplyDapRefreshPlan(const codium::DapRefreshPlan& plan)
    {
        UpdateDapStatus();
        if (plan.clearTransientViews) ClearDapTransientViews();
        if (plan.refreshThreads) {
            if (plan.threadId > 0) debugThreadId_ = plan.threadId;
            if (bottomWorkbench_) bottomWorkbench_->SetSelection(3);
            RequestDebugThreads();
        }
    }

    void ShowDebugMessage(const wxString& line)
    {
        ApplyDapRefreshPlan(dapSession_.ObserveMessage(line));
        if (debugConsole_) debugConsole_->AppendText(line + wxS("\n"));
        const wxString command = JsonStringField(line, wxS("command"));
        const wxString event = JsonStringField(line, wxS("event"));
        if (event == wxS("stopped")) {
            debugThreadId_ = JsonIntField(line, wxS("threadId"), debugThreadId_);
            return;
        }
        if (command == wxS("initialize")) {
            if (debugCapabilities_) {
                debugCapabilities_->Clear();
                const wxArrayString capabilities = {
                    wxS("supportsConfigurationDoneRequest"), wxS("supportsTerminateRequest"),
                    wxS("supportsSetVariable"), wxS("supportsEvaluateForHovers"),
                    wxS("supportsRestartRequest"), wxS("supportsStepBack"),
                    wxS("supportsFunctionBreakpoints"), wxS("supportsDataBreakpoints")};
                for (const auto& capability : capabilities) {
                    const wxString marker = wxString::Format(wxS("\"%s\":true"), capability);
                    debugCapabilities_->Append(capability + (line.Find(marker) != wxNOT_FOUND
                        ? T(wxS("label.capabilityYes")) : T(wxS("label.capabilityNo"))));
                }
            }
            supportsFunctionBreakpoints_ = line.Find(wxS("\"supportsFunctionBreakpoints\":true")) != wxNOT_FOUND;
            supportsDataBreakpoints_ = line.Find(wxS("\"supportsDataBreakpoints\":true")) != wxNOT_FOUND;
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
            if (!debugFrameLocations_.empty()) {
                SynchronizeActiveDebugFrame(debugFrameLocations_.front());
            }
            RequestDebugScopes();
        } else if (command == wxS("scopes")) {
            debugVariablesReference_ = JsonIntField(line, wxS("variablesReference"), debugVariablesReference_);
            RequestDebugVariables();
            RequestDebugWatches();
        } else if (command == wxS("variables") || command == wxS("evaluate")) {
            if (variables_) {
                variables_->Append(line);
                const wxArrayString names = JsonStringFields(line, wxS("name"));
                for (const auto& name : names) variables_->Append(name);
            }
        } else if (command == wxS("setBreakpoints")) {
            const int requestSequence = JsonIntField(line, wxS("request_seq"));
            wxString sourcePath = JsonStringField(line, wxS("path"));
            const auto pending = dapBreakpointRequests_.find(requestSequence);
            if (pending != dapBreakpointRequests_.end()) {
                sourcePath = pending->second;
                dapBreakpointRequests_.erase(pending);
            }
            if (!sourcePath.empty()) {
                wxString error;
                if (!dapSession_.ApplyBreakpointResponse(sourcePath, line, &error)) {
                    AppendLog(T(wxS("message.dapBreakpointParseError")) + error);
                } else if (!error.empty()) {
                    AppendLog(T(wxS("message.dapBreakpointResponse")) + error);
                } else {
                    AppendLog(T(wxS("message.dapBreakpointApplied")));
                }
                RefreshBreakpointView();
                RefreshGutters();
            } else {
                AppendLog(T(wxS("message.dapBreakpointMissingSource")));
            }
        } else if (command == wxS("setFunctionBreakpoints") || command == wxS("setDataBreakpoints")) {
            const int requestSequence = JsonIntField(line, wxS("request_seq"));
            const auto pending = dapAdvancedBreakpointRequests_.find(requestSequence);
            if (pending == dapAdvancedBreakpointRequests_.end()) {
                AppendLog(T(wxS("message.dapAdvancedBreakpointMissingRequest")));
                return;
            }
            const bool isFunction = pending->second == DapAdvancedBreakpointRequestKind::Function;
            dapAdvancedBreakpointRequests_.erase(pending);
            const auto messages = JsonStringFields(line, wxS("message"));
            const bool success = JsonBoolField(line, wxS("success"), true);
            const wxString fallback = isFunction ? T(wxS("message.functionBreakpointRejected"))
                                                 : T(wxS("message.dataBreakpointRejected"));
            if (isFunction) {
                const auto verified = JsonBoolFields(line, wxS("verified"));
                const auto ids = JsonIntFields(line, wxS("id"));
                for (size_t index = 0; index < functionBreakpoints_.size(); ++index) {
                    auto& breakpoint = functionBreakpoints_[index];
                    breakpoint.state = success && index < verified.size() && verified[index]
                        ? codium::DapBreakpointState::Verified : codium::DapBreakpointState::Rejected;
                    breakpoint.id = index < ids.size() ? ids[index] : 0;
                    breakpoint.message = index < messages.size() ? messages[index] : wxString();
                    if (!success && breakpoint.message.empty()) breakpoint.message = fallback;
                    if (success && breakpoint.state == codium::DapBreakpointState::Rejected && breakpoint.message.empty()) {
                        breakpoint.message = T(wxS("message.functionBreakpointUnverified"));
                    }
                }
            } else {
                const auto verified = JsonBoolFields(line, wxS("verified"));
                const auto ids = JsonIntFields(line, wxS("id"));
                for (size_t index = 0; index < dataBreakpoints_.size(); ++index) {
                    auto& breakpoint = dataBreakpoints_[index];
                    breakpoint.state = success && index < verified.size() && verified[index]
                        ? codium::DapBreakpointState::Verified : codium::DapBreakpointState::Rejected;
                    breakpoint.id = index < ids.size() ? ids[index] : 0;
                    breakpoint.message = index < messages.size() ? messages[index] : wxString();
                    if (!success && breakpoint.message.empty()) breakpoint.message = fallback;
                    if (success && breakpoint.state == codium::DapBreakpointState::Rejected && breakpoint.message.empty()) {
                        breakpoint.message = T(wxS("message.dataBreakpointUnverified"));
                    }
                }
            }
            PersistBreakpoints();
            RefreshBreakpointView();
            AppendLog(success ? T(wxS("message.dapAdvancedBreakpointApplied")) : fallback);
        }
    }

    void SynchronizeActiveDebugFrame(const DebugFrameLocation& frame)
    {
        debugFrameId_ = frame.id;
        const wxString mappedPath = sourceMapper_.Map(frame.path);
        activeDebugFramePath_ = mappedPath.empty() ? frame.path : mappedPath;
        activeDebugFrameLine_ = frame.line;
        activeDebugFrameCharacter_ = frame.character;
        if (!mappedPath.empty() && wxFileExists(mappedPath)) OpenDocumentPath(mappedPath);
        if (editor_) {
            const long position = codium::EditorActions::PositionForLineColumn(
                editor_->GetValue(), std::max(0, frame.line - 1), std::max(0, frame.character - 1));
            if (position != -1) {
                editor_->SetInsertionPoint(position);
                editor_->ShowPosition(position);
                editor_->SetFocus();
            }
        }
        UpdateDapStatus();
    }

    void GoToStackFrame(int index)
    {
        if (index < 0 || index >= static_cast<int>(debugFrameLocations_.size())) return;
        const DebugFrameLocation& frame = debugFrameLocations_[static_cast<size_t>(index)];
        SynchronizeActiveDebugFrame(frame);
        RequestDebugScopes();
    }

    wxString LspLocationLabel(const codium::LspLocation& location) const
    {
        const wxString path = codium::LspNavigation::UriToPath(
            location.uri.empty() ? DocumentUri() : location.uri);
        const wxString displayPath = workspace_.IsOpen() ? workspace_.RelativePath(path) : path;
        const wxString name = location.name.empty() ? displayPath : location.name;
        return wxString::Format(wxS("%s:%d:%d%s"), name, location.range.start.line + 1,
                                location.range.start.character + 1,
                                location.detail.empty() ? wxString() : wxS(" — ") + location.detail);
    }

    void OpenLspLocation(int index)
    {
        if (index < 0 || index >= static_cast<int>(lspLocations_.size())) return;
        const auto& location = lspLocations_[static_cast<size_t>(index)];
        const wxString path = codium::LspNavigation::UriToPath(
            location.uri.empty() ? DocumentUri() : location.uri);
        if (!path.empty() && wxFileExists(path)) OpenDocumentPath(path);
        if (!editor_) return;
        const long position = codium::EditorActions::PositionForLineColumn(
            editor_->GetValue(), location.range.start.line, location.range.start.character);
        editor_->SetInsertionPoint(position);
        editor_->ShowPosition(position);
        editor_->SetFocus();
        UpdateTitle();
    }

    void ApplyWorkspaceEdits(const std::vector<codium::LspTextEdit>& edits)
    {
        if (edits.empty()) {
            AppendLog(T(wxS("message.lspNoTextEdits")));
            return;
        }
        std::map<wxString, std::vector<codium::LspTextEdit>> grouped;
        for (const auto& edit : edits) {
            const wxString uri = edit.uri.empty() ? DocumentUri() : edit.uri;
            grouped[uri].push_back(edit);
        }
        for (const auto& [uri, fileEdits] : grouped) {
            const wxString path = codium::LspNavigation::UriToPath(uri);
            if (path.empty()) continue;
            if (path == document_.Path() && editor_) {
                const wxString updated = codium::LspNavigation::ApplyTextEdits(editor_->GetValue(), fileEdits);
                editor_->SetValue(updated);
                document_.SetText(updated);
                documents_[path].SetText(updated);
                NotifyLanguageDocumentChanged();
                UpdateTitle();
                continue;
            }
            codium::Document document;
            wxString error;
            if (!document.Load(path, &error)) {
                AppendLog(T(wxS("message.lspLoadEditError")) + error);
                continue;
            }
            document.SetText(codium::LspNavigation::ApplyTextEdits(document.Text(), fileEdits));
            if (!document.Save(&error)) AppendLog(T(wxS("message.lspSaveEditError")) + error);
            else if (documents_.find(path) != documents_.end()) documents_[path] = document;
        }
        ApplyInlineProblems();
        AppendLog(wxString::Format(T(wxS("message.lspTextEditsApplied")), edits.size()));
    }

    void ApplyCompletionItem(const codium::LspCompletionItem& item)
    {
        if (!editor_) return;
        codium::LspTextEdit edit = item.textEdit;
        if (!item.hasTextEdit) {
            const long caret = editor_->GetInsertionPoint();
            long start = caret;
            while (start > 0) {
                const wxChar character = editor_->GetValue()[static_cast<size_t>(start - 1)];
                if (!(std::isalnum(static_cast<unsigned char>(character)) || character == wxChar('_'))) break;
                --start;
            }
            const auto from = codium::EditorActions::LineColumnForPosition(editor_->GetValue(), start);
            const auto to = codium::EditorActions::LineColumnForPosition(editor_->GetValue(), caret);
            edit.uri = DocumentUri();
            edit.range = { {from.line, from.column}, {to.line, to.column} };
        }
        edit.newText = item.insertText.empty() ? item.label : item.insertText;
        ApplyWorkspaceEdits({edit});
        completion_->SetSelection(wxNOT_FOUND);
        lspResultMode_ = LspResultMode::None;
    }

    void ApplySelectedLspItem()
    {
        const int selection = completion_ ? completion_->GetSelection() : wxNOT_FOUND;
        if (selection == wxNOT_FOUND) return;
        if (lspResultMode_ == LspResultMode::Completion && selection < static_cast<int>(lspCompletionItems_.size())) {
            ApplyCompletionItem(lspCompletionItems_[static_cast<size_t>(selection)]);
        } else if (lspResultMode_ == LspResultMode::Locations) {
            OpenLspLocation(selection);
        } else if (lspResultMode_ == LspResultMode::CodeActions && selection < static_cast<int>(lspCodeActions_.size())) {
            ApplyWorkspaceEdits(lspCodeActions_[static_cast<size_t>(selection)].edits);
            lspResultMode_ = LspResultMode::None;
        }
    }

    void ShowLanguageResult(const wxString& line)
    {
        if (!hover_) {
            return;
        }
        const wxString method = JsonStringField(line, wxS("method"));
        if (method == wxS("textDocument/hover")) {
            const wxString value = codium::LspNavigation::HoverTextFromResult(line);
            hover_->SetValue(value.empty() ? T(wxS("label.noHoverInformation")) : value);
        } else if (method == wxS("textDocument/completion")) {
            lspCompletionItems_ = codium::LspNavigation::CompletionItemsFromResult(line);
            lspResultMode_ = LspResultMode::Completion;
            if (completion_) {
                completion_->Clear();
                for (const auto& item : lspCompletionItems_) {
                    completion_->Append(item.detail.empty() ? item.label : item.label + wxS(" — ") + item.detail);
                }
            }
            hover_->SetValue(wxString::Format(T(wxS("message.completionItems")),
                                              lspCompletionItems_.size()));
        } else if (method == wxS("textDocument/definition") || method == wxS("textDocument/declaration") ||
                   method == wxS("textDocument/references") || method == wxS("textDocument/documentSymbol") ||
                   method == wxS("workspace/symbol")) {
            lspLocations_ = codium::LspNavigation::LocationsFromResult(
                line, method == wxS("textDocument/documentSymbol"));
            for (auto& location : lspLocations_) {
                if (location.uri.empty()) location.uri = DocumentUri();
            }
            lspResultMode_ = LspResultMode::Locations;
            if (completion_) {
                completion_->Clear();
                for (const auto& location : lspLocations_) completion_->Append(LspLocationLabel(location));
            }
            hover_->SetValue(wxString::Format(T(wxS("message.lspLocations")),
                                              method, lspLocations_.size()));
            if ((method == wxS("textDocument/definition") || method == wxS("textDocument/declaration")) &&
                lspLocations_.size() == 1) {
                OpenLspLocation(0);
            }
        } else if (method == wxS("textDocument/rename")) {
            ApplyWorkspaceEdits(codium::LspNavigation::WorkspaceEditsFromResult(line));
        } else if (method == wxS("textDocument/codeAction")) {
            lspCodeActions_ = codium::LspNavigation::CodeActionsFromResult(line);
            lspResultMode_ = LspResultMode::CodeActions;
            if (completion_) {
                completion_->Clear();
                for (const auto& action : lspCodeActions_) completion_->Append(
                    action.kind.empty() ? action.title : action.title + wxS(" [") + action.kind + wxS("]"));
            }
            hover_->SetValue(wxString::Format(T(wxS("message.codeActions")),
                                              lspCodeActions_.size()));
        } else if (method == wxS("initialize")) {
            semanticTokenTypes_ = codium::SemanticTokenDecoder::TokenTypesFromInitialize(line);
            semanticTokensSupported_ = line.Find(wxS("\"semanticTokensProvider\"")) != wxNOT_FOUND;
            if (semanticTokensSupported_ && !document_.IsUntitled()) {
                host_.RequestLanguageSemanticTokens(DocumentUri());
            }
            hover_->SetValue(T(wxS("message.languageServerInitialized")));
        } else if (method == wxS("textDocument/semanticTokens/full")) {
            semanticTokens_ = codium::SemanticTokenDecoder::DecodeFullResponse(
                line, semanticTokenTypes_, document_.Text());
            semanticTokensSupported_ = true;
            ApplyInlineProblems();
            hover_->SetValue(wxString::Format(T(wxS("message.semanticTokensApplied")), semanticTokens_.size()));
        }
    }

    void OnTimer(wxTimerEvent&)
    {
        bool problemsChanged = false;
        for (const auto& line : taskRunner_.Poll()) {
            AppendLog(T(wxS("message.taskOutputPrefix")) + line);
            if (buildOutput_) buildOutput_->AppendText(line + wxS("\n"));
            AppendBuildSessionOutput(line);
            problemStore_.AddCompilerLine(line, taskRunner_.CurrentTask().name, WorkspaceDirectory(), activeBuildSessionId_);
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
            AppendLog(T(wxS("message.dapOutputPrefix")) + message);
            ShowDebugMessage(message);
        }
        for (const auto& line : host_.Poll()) {
            AppendLog(T(wxS("message.hostOutputPrefix")) + line);
            if (line.Find(wxS("\"event\":\"languageServerMessage\"")) != wxNOT_FOUND) {
                AppendLog(T(wxS("message.lspMessageReceived")));
            }
            if (line.Find(wxS("\"event\":\"diagnostics\"")) != wxNOT_FOUND) {
                AppendLog(T(wxS("message.lspDiagnosticsUpdated")));
                ShowDiagnostics(line);
                problemsChanged = true;
            }
            if (line.Find(wxS("\"event\":\"languageServerResult\"")) != wxNOT_FOUND) {
                ShowLanguageResult(line);
                if (line.Find(wxS("\"method\":\"textDocument/hover\"")) != wxNOT_FOUND) {
                    AppendLog(T(wxS("message.lspHoverReceived")));
                } else if (line.Find(wxS("\"method\":\"textDocument/completion\"")) != wxNOT_FOUND) {
                    AppendLog(T(wxS("message.lspCompletionReceived")));
                } else if (line.Find(wxS("\"method\":\"initialize\"")) != wxNOT_FOUND) {
                    AppendLog(T(wxS("message.lspInitialized")));
                }
            }
            if (line.Find(wxS("\"event\":\"contribution\"")) != wxNOT_FOUND &&
                line.Find(wxS("\"kind\":\"command\"")) != wxNOT_FOUND) {
                RegisterContributedCommand(line);
            }
            if (line.Find(wxS("\"event\":\"contribution\"")) != wxNOT_FOUND &&
                line.Find(wxS("\"kind\":\"view\"")) != wxNOT_FOUND) {
                RegisterContributedView(line);
            }
            if (line.Find(wxS("\"event\":\"treeView\"")) != wxNOT_FOUND) {
                UpdateContributedView(line);
            }
        }
        if (codeBlocksAdapter_.IsRunning()) {
            for (const auto& event : codeBlocksAdapter_.PollEvents()) {
                if (!codeBlocksAdapter_.IsReady()) {
                    AppendLog(T(wxS("message.codeBlocksContractRejected")));
                    codeBlocksAdapter_.Stop();
                    break;
                }
                HandleCodeBlocksEvent(event);
                problemsChanged = problemsChanged || event.kind == codium::CodeBlocksEventKind::CompilerDiagnostic ||
                                  event.kind == codium::CodeBlocksEventKind::CompilerOutput ||
                                  event.kind == codium::CodeBlocksEventKind::BuildStarted;
            }
            if (codeBlocksAdapter_.HandshakeReceived() && !codeBlocksAdapter_.IsReady()) {
                AppendLog(T(wxS("message.codeBlocksContractRejected")));
                codeBlocksAdapter_.Stop();
                adapterProjectOpened_ = false;
            }
            if (codeBlocksAdapter_.IsReady()) {
                if (!adapterProjectOpened_) {
                    const wxString projectFile = CodeBlocksProjectFile();
                    if (!projectFile.empty() && codeBlocksAdapter_.OpenProject(projectFile)) {
                        adapterProjectOpened_ = true;
                        AppendLog(T(wxS("message.codeBlocksProjectOpened")) + projectFile);
                    }
                }
                SetStatusText(wxString::Format(T(wxS("status.codeBlocksAdapterVersion")),
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
        PersistBreakpoints();
        taskRunner_.Stop();
        terminal_.Stop();
        dap_.Stop();
        codeBlocksAdapter_.Stop();
        adapterProjectOpened_ = false;
        host_.Stop();
        event.Skip();
    }

    wxString projectRoot_;
    codium::Localization localization_;
    codium::Workspace workspace_;
    codium::ExtensionHostClient host_;
    codium::ProjectConfig projectConfig_;
    codium::ProjectPreferences projectPreferences_;
    codium::BuildSessionStore buildSessions_;
    codium::TaskRunner taskRunner_;
    codium::TerminalSession terminal_;
    codium::TerminalScreen terminalScreen_;
    codium::DapClient dap_;
    codium::DapDebugSessionModel dapSession_;
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
    wxStaticText* buildSessionStatus_ = nullptr;
    wxListBox* buildSessionList_ = nullptr;
    int selectedSchemeIndex_ = wxNOT_FOUND;
    wxListBox* taskList_ = nullptr;
    wxListBox* breakpoints_ = nullptr;
    wxListBox* debugThreads_ = nullptr;
    wxListBox* callStack_ = nullptr;
    wxListBox* variables_ = nullptr;
    wxListBox* watches_ = nullptr;
    wxListBox* debugCapabilities_ = nullptr;
    wxStaticText* debugStatus_ = nullptr;
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
    wxString lastSearchQuery_;
    wxString lastReplacement_;
    wxString lastSymbolQuery_;
    codium::ThemeKind themeKind_ = codium::ThemeKind::System;
    codium::ThemePalette themePalette_ = codium::ThemePalette::For(codium::ThemeKind::System);
    bool lastSearchMatchCase_ = true;
    wxTimer timer_;
    bool loadingDocument_ = false;
    int documentVersion_ = 1;
    bool languageServerInitialized_ = false;
    wxString languageId_ = wxS("plaintext");
    bool semanticTokensSupported_ = false;
    wxArrayString semanticTokenTypes_;
    std::vector<codium::SemanticToken> semanticTokens_;
    LspResultMode lspResultMode_ = LspResultMode::None;
    std::vector<codium::LspCompletionItem> lspCompletionItems_;
    std::vector<codium::LspLocation> lspLocations_;
    std::vector<codium::LspCodeAction> lspCodeActions_;
    std::vector<long> matchingDelimiterPositions_;
    wxColour ansiColour_ = wxColour(230, 230, 230);
    std::vector<wxString> terminalHistory_;
    size_t historyIndex_ = 0;
    wxPoint terminalSelectionAnchor_;
    wxPoint terminalSelectionActive_;
    bool terminalSelecting_ = false;
    std::map<int, wxString> dapBreakpointRequests_;
    std::vector<codium::DapFunctionBreakpoint> functionBreakpoints_;
    std::vector<codium::DapDataBreakpoint> dataBreakpoints_;
    bool supportsFunctionBreakpoints_ = false;
    bool supportsDataBreakpoints_ = false;
    std::map<int, DapAdvancedBreakpointRequestKind> dapAdvancedBreakpointRequests_;
    std::vector<std::pair<int, int>> problemLocations_;
    std::vector<wxString> problemPaths_;
    int debugThreadId_ = 1;
    int debugFrameId_ = 1;
    int debugVariablesReference_ = 1;
    codium::SourceMapper sourceMapper_;
    wxArrayString watchExpressions_;
    std::vector<DebugFrameLocation> debugFrameLocations_;
    wxString activeDebugFramePath_;
    int activeDebugFrameLine_ = 0;
    int activeDebugFrameCharacter_ = 0;
    std::vector<size_t> problemIndices_;
    codium::ProjectTask lastBuildTask_;
    bool hasLastBuildTask_ = false;
    wxString activeBuildSessionId_;
    bool pendingRunAfterBuild_ = false;
    codium::ProjectTarget pendingRunTarget_;
    wxString pendingRunTaskName_;
    wxString pendingRunConfiguration_;
    wxString pendingRunArtifactOverride_;

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
