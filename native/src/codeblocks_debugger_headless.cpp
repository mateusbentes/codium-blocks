// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#include "codium/codeblocks_debugger_headless.hpp"

#include <cbdebugger_interfaces.h>
#include <cbplugin.h>

namespace codium {
namespace {

class HeadlessBacktrace final : public cbBacktraceDlg {
public:
    wxWindow* GetWindow() override { return nullptr; }
    void Reload() override {}
    void EnableWindow(bool) override {}
};

class HeadlessBreakpoints final : public cbBreakpointsDlg {
public:
    wxWindow* GetWindow() override { return nullptr; }
    bool AddBreakpoint(cbDebuggerPlugin*, const wxString&, int) override { return false; }
    bool RemoveBreakpoint(cbDebuggerPlugin*, const wxString&, int) override { return false; }
    void RemoveAllBreakpoints() override {}
    void EditBreakpoint(const wxString&, int) override {}
    void EnableBreakpoint(const wxString&, int, bool) override {}
    void Reload() override {}
};

class HeadlessRegisters final : public cbCPURegistersDlg {
public:
    wxWindow* GetWindow() override { return nullptr; }
    void Clear() override {}
    void SetRegisterValue(const wxString&, const wxString&, const wxString&) override {}
    void EnableWindow(bool) override {}
};

class HeadlessDisassembly final : public cbDisassemblyDlg {
public:
    wxWindow* GetWindow() override { return nullptr; }
    void Clear(const cbStackFrame&) override {}
    void AddAssemblerLine(uint64_t, const wxString&) override {}
    void AddSourceLine(int, const wxString&) override {}
    bool SetActiveAddress(uint64_t) override { return false; }
    void CenterLine(int) override {}
    void CenterCurrentLine() override {}
    bool HasActiveAddr() override { return false; }
    void EnableWindow(bool) override {}
};

class HeadlessMemory final : public cbExamineMemoryDlg {
public:
    wxWindow* GetWindow() override { return nullptr; }
    void Begin() override {}
    void End() override {}
    void Clear() override {}
    wxString GetBaseAddress() override { return wxEmptyString; }
    int GetBytes() override { return 0; }
    void AddError(const wxString&) override {}
    void AddHexByte(const wxString&, const wxString&) override {}
    void EnableWindow(bool) override {}
    void SetBaseAddress(const wxString&) override {}
};

class HeadlessThreads final : public cbThreadsDlg {
public:
    wxWindow* GetWindow() override { return nullptr; }
    void Reload() override {}
    void EnableWindow(bool) override {}
};

class HeadlessWatches final : public cbWatchesDlg {
public:
    wxWindow* GetWindow() override { return nullptr; }
    void AddWatch(cb::shared_ptr<cbWatch>) override {}
    void AddSpecialWatch(cb::shared_ptr<cbWatch>, bool) override {}
    void RemoveWatch(cb::shared_ptr<cbWatch>) override {}
    void RenameWatch(wxObject*, const wxString&) override {}
    void RefreshUI() override {}
};

class HeadlessInterfaceFactory final : public cbDebugInterfaceFactory {
public:
    cbBacktraceDlg* CreateBacktrace() override { return new HeadlessBacktrace; }
    void DeleteBacktrace(cbBacktraceDlg* value) override { delete value; }

    cbBreakpointsDlg* CreateBreapoints() override { return new HeadlessBreakpoints; }
    void DeleteBreakpoints(cbBreakpointsDlg* value) override { delete value; }

    cbCPURegistersDlg* CreateCPURegisters() override { return new HeadlessRegisters; }
    void DeleteCPURegisters(cbCPURegistersDlg* value) override { delete value; }

    cbDisassemblyDlg* CreateDisassembly() override { return new HeadlessDisassembly; }
    void DeleteDisassembly(cbDisassemblyDlg* value) override { delete value; }

    cbExamineMemoryDlg* CreateMemory() override { return new HeadlessMemory; }
    void DeleteMemory(cbExamineMemoryDlg* value) override { delete value; }

    cbThreadsDlg* CreateThreads() override { return new HeadlessThreads; }
    void DeleteThreads(cbThreadsDlg* value) override { delete value; }

    cbWatchesDlg* CreateWatches() override { return new HeadlessWatches; }
    void DeleteWatches(cbWatchesDlg* value) override { delete value; }

    bool ShowValueTooltip(const cb::shared_ptr<cbWatch>&, const wxRect&) override { return false; }
    void HideValueTooltip() override {}
    bool IsValueTooltipShown() override { return false; }
    void UpdateValueTooltip() override {}
};

class HeadlessMenuHandler final : public cbDebuggerMenuHandler {
public:
    void SetActiveDebugger(cbDebuggerPlugin*) override {}
    void MarkActiveTargetAsValid(bool) override {}
    void RebuildMenus() override {}
    void BuildContextMenu(wxMenu&, const wxString&, bool) override {}
    bool RegisterWindowMenu(const wxString&, const wxString&, cbDebuggerWindowMenuItem* item) override
    {
        delete item;
        return false;
    }
    void UnregisterWindowMenu(const wxString&) override {}
};

} // namespace

cbDebugInterfaceFactory* CreateHeadlessDebuggerInterfaceFactory()
{
    return new HeadlessInterfaceFactory;
}

cbDebuggerMenuHandler* CreateHeadlessDebuggerMenuHandler()
{
    return new HeadlessMenuHandler;
}

} // namespace codium
