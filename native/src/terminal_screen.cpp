#include "codium/terminal_screen.hpp"

#include <wx/utils.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>

namespace codium {

namespace {

std::vector<int> ParseParameters(const wxString& value)
{
    std::vector<int> values;
    wxString token;
    for (size_t i = 0; i <= value.length(); ++i) {
        const wxChar character = i < value.length() ? static_cast<wxChar>(value[i]) : wxChar(';');
        if (character == wxChar(';')) {
            values.push_back(token.empty() ? 0 : wxAtoi(token));
            token.clear();
        } else if (character >= wxChar('0') && character <= wxChar('9')) {
            token += character;
        }
    }
    return values;
}

int ClampIndex(int value)
{
    return std::max(0, std::min(255, value));
}

} // namespace

TerminalScreen::TerminalScreen(int columns, int rows)
    : columns_(std::max(1, columns)), rows_(std::max(1, rows))
{
    primaryGrid_.resize(static_cast<size_t>(columns_ * rows_));
    alternateGrid_.resize(static_cast<size_t>(columns_ * rows_));
    ClearGrid(primaryGrid_);
    ClearGrid(alternateGrid_);
}

void TerminalScreen::ClearGrid(std::vector<TerminalCell>& grid)
{
    grid.assign(static_cast<size_t>(columns_ * rows_), TerminalCell{});
}

void TerminalScreen::Resize(int columns, int rows)
{
    columns = std::max(1, columns);
    rows = std::max(1, rows);
    if (columns == columns_ && rows == rows_) return;
    columns_ = columns;
    rows_ = rows;
    ClearGrid(primaryGrid_);
    ClearGrid(alternateGrid_);
    cursorColumn_ = std::min(cursorColumn_, columns_ - 1);
    cursorRow_ = std::min(cursorRow_, rows_ - 1);
}

void TerminalScreen::Reset()
{
    cursorColumn_ = 0;
    cursorRow_ = 0;
    savedColumn_ = 0;
    savedRow_ = 0;
    alternateSavedColumn_ = 0;
    alternateSavedRow_ = 0;
    cursorVisible_ = true;
    wrapEnabled_ = true;
    alternateScreen_ = false;
    parserState_ = ParserState::Ground;
    csiParameters_.clear();
    oscBuffer_.clear();
    foreground_ = 7;
    background_ = 0;
    bold_ = false;
    underline_ = false;
    inverse_ = false;
    mouseReporting_ = false;
    sgrMouse_ = false;
    bracketedPaste_ = false;
    scrollOffset_ = 0;
    scrollback_.clear();
    ClearGrid(primaryGrid_);
    ClearGrid(alternateGrid_);
}

std::vector<TerminalCell>& TerminalScreen::Grid()
{
    return alternateScreen_ ? alternateGrid_ : primaryGrid_;
}

const std::vector<TerminalCell>& TerminalScreen::Grid() const
{
    return alternateScreen_ ? alternateGrid_ : primaryGrid_;
}

const TerminalCell& TerminalScreen::CellAt(int column, int row) const
{
    static const TerminalCell blank;
    if (column < 0 || row < 0 || column >= columns_ || row >= rows_) return blank;
    return Grid()[static_cast<size_t>(row * columns_ + column)];
}

const TerminalCell& TerminalScreen::VisibleCellAt(int column, int row) const
{
    static const TerminalCell blank;
    if (column < 0 || row < 0 || column >= columns_ || row >= rows_) return blank;
    if (scrollOffset_ == 0) return CellAt(column, row);
    const int historyRows = static_cast<int>(scrollback_.size());
    const int sourceRow = historyRows - scrollOffset_ + row;
    if (sourceRow < 0) return blank;
    if (sourceRow < historyRows) return scrollback_[static_cast<size_t>(sourceRow)][static_cast<size_t>(column)];
    return CellAt(column, sourceRow - historyRows);
}

void TerminalScreen::ScrollBack(int lines)
{
    scrollOffset_ = std::min(static_cast<int>(scrollback_.size()),
                              std::max(0, scrollOffset_ + std::max(1, lines)));
}

void TerminalScreen::ScrollForward(int lines)
{
    scrollOffset_ = std::max(0, scrollOffset_ - std::max(1, lines));
}

wxColour TerminalScreen::PaletteColor(int index, bool bold)
{
    static const std::array<wxColour, 16> colors = {
        wxColour(0, 0, 0), wxColour(205, 49, 49), wxColour(13, 188, 121), wxColour(229, 229, 16),
        wxColour(36, 114, 200), wxColour(188, 63, 188), wxColour(17, 168, 205), wxColour(229, 229, 229),
        wxColour(102, 102, 102), wxColour(241, 76, 76), wxColour(35, 209, 139), wxColour(245, 245, 67),
        wxColour(59, 142, 234), wxColour(214, 112, 214), wxColour(41, 184, 219), wxColour(255, 255, 255)
    };
    if (index >= 0 && index < 16) {
        return colors[static_cast<size_t>((bold && index < 8) ? index + 8 : index)];
    }
    if (index >= 16 && index < 232) {
        const int value = index - 16;
        const int red = (value / 36) == 0 ? 0 : 55 + ((value / 36) - 1) * 40;
        const int green = ((value / 6) % 6) == 0 ? 0 : 55 + (((value / 6) % 6) - 1) * 40;
        const int blue = (value % 6) == 0 ? 0 : 55 + ((value % 6) - 1) * 40;
        return wxColour(red, green, blue);
    }
    const int gray = 8 + (index - 232) * 10;
    return wxColour(gray, gray, gray);
}

void TerminalScreen::MoveCursor(int column, int row)
{
    cursorColumn_ = std::max(0, std::min(columns_ - 1, column));
    cursorRow_ = std::max(0, std::min(rows_ - 1, row));
}

bool TerminalScreen::IsCombining(wxChar character)
{
    const uint32_t code = static_cast<uint32_t>(character);
    return (code >= 0x0300 && code <= 0x036f) || (code >= 0x1ab0 && code <= 0x1aff) ||
           (code >= 0x1dc0 && code <= 0x1dff) || (code >= 0x20d0 && code <= 0x20ff) ||
           (code >= 0xfe20 && code <= 0xfe2f);
}

bool TerminalScreen::IsWide(wxChar character)
{
    const uint32_t code = static_cast<uint32_t>(character);
    return (code >= 0x1100 && code <= 0x115f) || (code >= 0x2329 && code <= 0x232a) ||
           (code >= 0x2e80 && code <= 0xa4cf) || (code >= 0xac00 && code <= 0xd7a3) ||
           (code >= 0xf900 && code <= 0xfaff) || (code >= 0xfe10 && code <= 0xfe19) ||
           (code >= 0xfe30 && code <= 0xfe6f) || (code >= 0xff00 && code <= 0xff60) ||
           (code >= 0xffe0 && code <= 0xffe6) || (code >= 0x1f300 && code <= 0x1faff);
}

void TerminalScreen::PushScrollbackRow()
{
    if (alternateScreen_) return;
    scrollOffset_ = 0;
    if (scrollback_.size() >= maxScrollback_) scrollback_.pop_front();
    scrollback_.push_back(std::vector<TerminalCell>(Grid().begin(), Grid().begin() + columns_));
}

void TerminalScreen::ScrollUp(int count)
{
    count = std::max(1, count);
    count = std::min(count, rows_);
    auto& grid = Grid();
    for (int index = 0; index < count; ++index) PushScrollbackRow();
    grid.erase(grid.begin(), grid.begin() + static_cast<ptrdiff_t>(count * columns_));
    grid.insert(grid.end(), static_cast<size_t>(count * columns_), TerminalCell{});
}

void TerminalScreen::LineFeed()
{
    if (cursorRow_ == rows_ - 1) ScrollUp();
    else ++cursorRow_;
}

void TerminalScreen::CarriageReturn()
{
    cursorColumn_ = 0;
}

void TerminalScreen::Backspace()
{
    cursorColumn_ = std::max(0, cursorColumn_ - 1);
}

void TerminalScreen::PutCharacter(wxChar character)
{
    if (character < 0x20) return;
    if (IsCombining(character) && cursorColumn_ > 0) {
        TerminalCell& previous = Grid()[static_cast<size_t>(cursorRow_ * columns_ + cursorColumn_ - 1)];
        if (previous.text.empty()) previous.text += previous.character;
        previous.text += character;
        return;
    }
    const int width = IsWide(character) ? 2 : 1;
    if (width == 2 && cursorColumn_ == columns_ - 1 && wrapEnabled_) {
        cursorColumn_ = 0;
        LineFeed();
    }
    if (cursorColumn_ >= columns_) {
        if (wrapEnabled_) {
            cursorColumn_ = 0;
            LineFeed();
        } else {
            cursorColumn_ = columns_ - 1;
        }
    }
    TerminalCell& cell = Grid()[static_cast<size_t>(cursorRow_ * columns_ + cursorColumn_)];
    cell.character = character;
    cell.text.clear();
    cell.text += character;
    cell.foreground = ClampIndex(foreground_);
    cell.background = ClampIndex(background_);
    cell.bold = bold_;
    cell.underline = underline_;
    cell.inverse = inverse_;
    cell.width = width;
    cell.continuation = false;
    if (width == 2 && cursorColumn_ + 1 < columns_) {
        TerminalCell& continuation = Grid()[static_cast<size_t>(cursorRow_ * columns_ + cursorColumn_ + 1)];
        continuation = TerminalCell{};
        continuation.continuation = true;
        continuation.width = 0;
        ++cursorColumn_;
    }
    ++cursorColumn_;
}

int TerminalScreen::Parameter(size_t index, int fallback) const
{
    const std::vector<int> values = ParseParameters(csiParameters_);
    if (index >= values.size() || values[index] == 0) return fallback;
    return values[index];
}

void TerminalScreen::EraseDisplay(int mode)
{
    auto& grid = Grid();
    if (mode == 2 || mode == 3) {
        ClearGrid(grid);
        MoveCursor(0, 0);
        return;
    }
    if (mode == 0) {
        for (int row = cursorRow_; row < rows_; ++row) {
            const int start = row == cursorRow_ ? cursorColumn_ : 0;
            for (int column = start; column < columns_; ++column) grid[static_cast<size_t>(row * columns_ + column)] = TerminalCell{};
        }
    } else if (mode == 1) {
        for (int row = 0; row <= cursorRow_; ++row) {
            const int end = row == cursorRow_ ? cursorColumn_ : columns_ - 1;
            for (int column = 0; column <= end; ++column) grid[static_cast<size_t>(row * columns_ + column)] = TerminalCell{};
        }
    }
}

void TerminalScreen::EraseLine(int mode)
{
    auto& grid = Grid();
    int start = 0;
    int end = columns_ - 1;
    if (mode == 0) start = cursorColumn_;
    else if (mode == 1) end = cursorColumn_;
    for (int column = start; column <= end; ++column) grid[static_cast<size_t>(cursorRow_ * columns_ + column)] = TerminalCell{};
}

void TerminalScreen::HandleSgr()
{
    const std::vector<int> values = ParseParameters(csiParameters_.empty() ? wxString(wxS("0")) : csiParameters_);
    if (values.empty()) return;
    for (size_t i = 0; i < values.size(); ++i) {
        const int value = values[i];
        if (value == 0) { foreground_ = 7; background_ = 0; bold_ = false; underline_ = false; inverse_ = false; }
        else if (value == 1) bold_ = true;
        else if (value == 22) bold_ = false;
        else if (value == 4) underline_ = true;
        else if (value == 24) underline_ = false;
        else if (value == 7) inverse_ = true;
        else if (value == 27) inverse_ = false;
        else if (value >= 30 && value <= 37) foreground_ = value - 30;
        else if (value == 39) foreground_ = 7;
        else if (value >= 40 && value <= 47) background_ = value - 40;
        else if (value == 49) background_ = 0;
        else if (value >= 90 && value <= 97) foreground_ = value - 90 + 8;
        else if (value >= 100 && value <= 107) background_ = value - 100 + 8;
        else if ((value == 38 || value == 48) && i + 1 < values.size()) {
            const bool foreground = value == 38;
            if (values[i + 1] == 5 && i + 2 < values.size()) {
                if (foreground) foreground_ = ClampIndex(values[i + 2]); else background_ = ClampIndex(values[i + 2]);
                i += 2;
            } else if (values[i + 1] == 2 && i + 4 < values.size()) {
                // Map true-color approximately to the closest xterm 256-color index.
                const int r = values[i + 2];
                const int g = values[i + 3];
                const int b = values[i + 4];
                const int mapped = 16 + ((r * 5 / 255) * 36) + ((g * 5 / 255) * 6) + (b * 5 / 255);
                if (foreground) foreground_ = mapped; else background_ = mapped;
                i += 4;
            }
        }
    }
}

void TerminalScreen::SwitchAlternateScreen(bool enable)
{
    if (enable == alternateScreen_) return;
    if (enable) {
        alternateSavedColumn_ = cursorColumn_;
        alternateSavedRow_ = cursorRow_;
    }
    alternateScreen_ = enable;
    if (enable) {
        ClearGrid(alternateGrid_);
        cursorColumn_ = 0;
        cursorRow_ = 0;
    } else {
        MoveCursor(alternateSavedColumn_, alternateSavedRow_);
    }
}

void TerminalScreen::HandleMode(bool set)
{
    const wxString parameters = csiParameters_.StartsWith(wxS("?")) ? csiParameters_.Mid(1) : csiParameters_;
    const std::vector<int> values = ParseParameters(parameters);
    for (const int value : values) {
        if (value == 25) cursorVisible_ = set;
        else if (value == 7) wrapEnabled_ = set;
        else if (value == 47 || value == 1047 || value == 1049) SwitchAlternateScreen(set);
        else if (value == 1000 || value == 1002 || value == 1003) mouseReporting_ = set;
        else if (value == 1006) sgrMouse_ = set;
        else if (value == 2004) bracketedPaste_ = set;
    }
}

void TerminalScreen::HandleCsi(wxChar finalCharacter)
{
    const bool privateMode = csiParameters_.StartsWith(wxS("?"));
    if (privateMode && (finalCharacter == wxChar('h') || finalCharacter == wxChar('l'))) {
        HandleMode(finalCharacter == wxChar('h'));
        return;
    }
    switch (static_cast<wchar_t>(finalCharacter)) {
    case 'A': MoveCursor(cursorColumn_, cursorRow_ - Parameter(0)); break;
    case 'B': MoveCursor(cursorColumn_, cursorRow_ + Parameter(0)); break;
    case 'C': MoveCursor(cursorColumn_ + Parameter(0), cursorRow_); break;
    case 'D': MoveCursor(cursorColumn_ - Parameter(0), cursorRow_); break;
    case 'E': MoveCursor(0, cursorRow_ + Parameter(0)); break;
    case 'F': MoveCursor(0, cursorRow_ - Parameter(0)); break;
    case 'G': MoveCursor(Parameter(0) - 1, cursorRow_); break;
    case 'd': MoveCursor(cursorColumn_, Parameter(0) - 1); break;
    case 'H': case 'f': MoveCursor(Parameter(1) - 1, Parameter(0) - 1); break;
    case 'J': EraseDisplay(ParseParameters(csiParameters_).empty() ? 0 : ParseParameters(csiParameters_)[0]); break;
    case 'K': EraseLine(ParseParameters(csiParameters_).empty() ? 0 : ParseParameters(csiParameters_)[0]); break;
    case 'P': {
        const int count = Parameter(0);
        auto& grid = Grid();
        const int start = cursorRow_ * columns_ + cursorColumn_;
        const int end = cursorRow_ * columns_ + columns_;
        for (int index = start; index < end; ++index) grid[static_cast<size_t>(index)] = index + count < end ? grid[static_cast<size_t>(index + count)] : TerminalCell{};
        break;
    }
    case 's': savedColumn_ = cursorColumn_; savedRow_ = cursorRow_; break;
    case 'u': MoveCursor(savedColumn_, savedRow_); break;
    case 'h': wrapEnabled_ = true; break;
    case 'l': wrapEnabled_ = false; break;
    case 'm': HandleSgr(); break;
    default: break;
    }
}

void TerminalScreen::Feed(const wxString& bytes)
{
    for (size_t index = 0; index < bytes.length(); ++index) {
        const wxChar character = bytes[index];
        switch (parserState_) {
        case ParserState::Ground:
            if (character == wxChar(0x1b)) parserState_ = ParserState::Escape;
            else if (character == wxChar('\n')) LineFeed();
            else if (character == wxChar('\r')) CarriageReturn();
            else if (character == wxChar('\b')) Backspace();
            else if (character == wxChar('\t')) MoveCursor(std::min(columns_ - 1, ((cursorColumn_ / 8) + 1) * 8), cursorRow_);
            else if (character == wxChar('\a')) {}
            else if (character >= wxChar(' ')) PutCharacter(character);
            break;
        case ParserState::Escape:
            if (character == wxChar('[')) { csiParameters_.clear(); parserState_ = ParserState::Csi; }
            else if (character == wxChar(']')) { oscBuffer_.clear(); parserState_ = ParserState::Osc; }
            else if (character == wxChar('7')) { savedColumn_ = cursorColumn_; savedRow_ = cursorRow_; parserState_ = ParserState::Ground; }
            else if (character == wxChar('8')) { MoveCursor(savedColumn_, savedRow_); parserState_ = ParserState::Ground; }
            else if (character == wxChar('D')) { LineFeed(); parserState_ = ParserState::Ground; }
            else if (character == wxChar('M')) { if (cursorRow_ == 0) { auto& grid = Grid(); grid.insert(grid.begin(), static_cast<size_t>(columns_), TerminalCell{}); grid.pop_back(); } else --cursorRow_; parserState_ = ParserState::Ground; }
            else if (character == wxChar('c')) { Reset(); parserState_ = ParserState::Ground; }
            else parserState_ = ParserState::Ground;
            break;
        case ParserState::Csi:
            if (character >= wxChar('@') && character <= wxChar('~')) { HandleCsi(character); csiParameters_.clear(); parserState_ = ParserState::Ground; }
            else csiParameters_ += character;
            break;
        case ParserState::Osc:
            if (character == wxChar('\a')) { oscBuffer_.clear(); parserState_ = ParserState::Ground; }
            else if (character == wxChar(0x1b)) parserState_ = ParserState::OscEscape;
            else oscBuffer_ += character;
            break;
        case ParserState::OscEscape:
            parserState_ = character == wxChar('\\') ? ParserState::Ground : ParserState::Osc;
            break;
        }
    }
}

} // namespace codium
