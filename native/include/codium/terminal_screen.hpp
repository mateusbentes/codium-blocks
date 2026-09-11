// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

#pragma once

#include <wx/colour.h>
#include <wx/string.h>

#include <vector>
#include <deque>
#include <cstdint>

namespace codium {

struct TerminalCell final {
    wxChar character = wxChar(' ');
    wxString text;
    int foreground = 7;
    int background = 0;
    bool bold = false;
    bool underline = false;
    bool inverse = false;
    wxString hyperlink;
    int width = 1;
    bool continuation = false;
};

class TerminalScreen final {
public:
    TerminalScreen(int columns = 120, int rows = 32);

    void Resize(int columns, int rows);
    void Reset();
    bool Feed(const wxString& bytes);

    int Columns() const { return columns_; }
    int Rows() const { return rows_; }
    int CursorColumn() const { return cursorColumn_; }
    int CursorRow() const { return cursorRow_; }
    bool CursorVisible() const { return cursorVisible_; }
    bool AlternateScreen() const { return alternateScreen_; }
    const TerminalCell& CellAt(int column, int row) const;
    const TerminalCell& VisibleCellAt(int column, int row) const;
    void ScrollBack(int lines);
    void ScrollForward(int lines);
    int ScrollbackSize() const { return static_cast<int>(scrollback_.size()); }
    int ScrollOffset() const { return scrollOffset_; }
    bool MouseReporting() const { return mouseReporting_; }
    bool SgrMouse() const { return sgrMouse_; }
    bool BracketedPaste() const { return bracketedPaste_; }
    bool SynchronizedUpdates() const { return synchronizedUpdates_; }
    bool GraphicsDiscarded() const { return graphicsDiscarded_; }

    static wxColour PaletteColor(int index, bool bold = false);

private:
    enum class ParserState { Ground, Escape, Csi, Osc, OscEscape, Dcs, DcsEscape, Apc, ApcEscape };

    std::vector<TerminalCell>& Grid();
    const std::vector<TerminalCell>& Grid() const;
    void ClearGrid(std::vector<TerminalCell>& grid);
    void PutCharacter(wxChar character);
    void PutText(const wxString& text, uint32_t codepoint);
    void LineFeed();
    void CarriageReturn();
    void Backspace();
    void MoveCursor(int column, int row);
    void ScrollUp(int count = 1);
    void EraseDisplay(int mode);
    void EraseLine(int mode);
    void HandleCsi(wxChar finalCharacter);
    void HandleSgr();
    void HandleMode(bool set);
    void SwitchAlternateScreen(bool enable);
    void HandleOsc();
    void PushScrollbackRow();
    static bool IsCombining(wxChar character);
    static bool IsWide(wxChar character);
    static bool IsRegionalIndicator(wxChar character);
    static bool IsCombiningCodepoint(uint32_t codepoint);
    static bool IsWideCodepoint(uint32_t codepoint);
    static bool IsRegionalIndicatorCodepoint(uint32_t codepoint);
    int Parameter(size_t index, int fallback = 1) const;

    int columns_;
    int rows_;
    int cursorColumn_ = 0;
    int cursorRow_ = 0;
    int savedColumn_ = 0;
    int savedRow_ = 0;
    int alternateSavedColumn_ = 0;
    int alternateSavedRow_ = 0;
    bool cursorVisible_ = true;
    bool wrapEnabled_ = true;
    bool alternateScreen_ = false;
    bool mouseReporting_ = false;
    bool sgrMouse_ = false;
    bool bracketedPaste_ = false;
    bool synchronizedUpdates_ = false;
    bool graphicsDiscarded_ = false;
    ParserState parserState_ = ParserState::Ground;
    wxString csiParameters_;
    wxString oscBuffer_;

    int foreground_ = 7;
    int background_ = 0;
    bool bold_ = false;
    bool underline_ = false;
    bool inverse_ = false;
    wxString hyperlink_;
    bool graphemeJoinPending_ = false;
    bool regionalIndicatorPending_ = false;
    std::vector<TerminalCell> primaryGrid_;
    std::vector<TerminalCell> alternateGrid_;
    std::deque<std::vector<TerminalCell>> scrollback_;
    int scrollOffset_ = 0;
    size_t maxScrollback_ = 2000;
};

} // namespace codium
