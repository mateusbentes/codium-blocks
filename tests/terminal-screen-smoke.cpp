#include "codium/terminal_screen.hpp"

#include <iostream>

namespace {

bool Cell(const codium::TerminalScreen& screen, int column, int row, wxChar expected)
{
    return screen.CellAt(column, row).character == expected;
}

} // namespace

int main()
{
    codium::TerminalScreen screen(10, 3);
    screen.Feed(wxS("hello"));
    if (!Cell(screen, 0, 0, 'h') || !Cell(screen, 4, 0, 'o') || screen.CursorColumn() != 5) return 1;

    screen.Feed(wxS("\x1b[2;3HZ"));
    if (!Cell(screen, 2, 1, 'Z') || screen.CursorRow() != 1) return 2;

    screen.Feed(wxS("\x1b[31mR\x1b[0m"));
    if (!Cell(screen, 3, 1, 'R') || screen.CellAt(3, 1).foreground != 1) return 3;

    screen.Feed(wxS("\x1b[2J"));
    if (!Cell(screen, 0, 0, ' ') || screen.CursorColumn() != 0 || screen.CursorRow() != 0) return 4;

    screen.Feed(wxS("xy"));
    screen.Feed(wxS("a\r\nb\r\nc\r\nd"));
    if (!Cell(screen, 0, 0, 'b') || !Cell(screen, 0, 1, 'c') || !Cell(screen, 0, 2, 'd')) return 5;

    const int savedColumn = screen.CursorColumn();
    const int savedRow = screen.CursorRow();
    screen.Feed(wxS("\x1b[?1049hALT\x1b[?1049l"));
    if (screen.AlternateScreen() || !Cell(screen, 0, 0, 'b') ||
        screen.CursorColumn() != savedColumn || screen.CursorRow() != savedRow) return 6;

    screen.Feed(wxS("\x1b[?25l"));
    if (screen.CursorVisible()) return 7;
    screen.Feed(wxS("\x1b[?25h"));
    if (!screen.CursorVisible()) return 8;

    screen.Feed(wxS("\x1b[?1000h\x1b[?1006h\x1b[?2004h"));
    if (!screen.MouseReporting() || !screen.SgrMouse() || !screen.BracketedPaste()) return 9;

    screen.Reset();
    screen.Feed(wxString::FromUTF8("e\xCC\x81"));
    if (screen.CellAt(0, 0).text.length() < 2) return 10;
    screen.Reset();
    screen.Feed(wxString::FromUTF8("\xE7\x95\x8C"));
    if (screen.CellAt(0, 0).width != 2 || !screen.CellAt(1, 0).continuation) return 11;

    screen.Reset();
    screen.Feed(wxS("one\r\ntwo\r\nthree\r\nfour"));
    if (screen.ScrollbackSize() == 0) return 12;
    screen.ScrollBack(1);
    if (screen.VisibleCellAt(0, 0).character != wxChar('o')) return 13;

    screen.Reset();
    screen.Feed(wxString::FromUTF8("\x1b]8;;https://example.com\x1b\\link\x1b]8;;\x1b\\"));
    if (screen.CellAt(0, 0).hyperlink != wxS("https://example.com")) return 14;
    screen.Reset();
    screen.Feed(wxString::FromUTF8("\x1b]8;;javascript:alert(1)\x1b\\unsafe\x1b]8;;\x1b\\"));
    if (!screen.CellAt(0, 0).hyperlink.empty()) return 15;

    screen.Reset();
    if (screen.Feed(wxString::FromUTF8("\x1b[?2026hbatched"))) return 16;
    if (!screen.SynchronizedUpdates()) return 17;
    if (!screen.Feed(wxString::FromUTF8("\x1b[?2026l")) || screen.SynchronizedUpdates()) return 18;

    screen.Reset();
    screen.Feed(wxString::FromUTF8("\x1bPq1;2;3;4;5\x1b\\"));
    if (!screen.GraphicsDiscarded() || !Cell(screen, 0, 0, ' ')) return 19;

    screen.Reset();
    screen.Feed(wxString::FromUTF8("\xF0\x9F\x87\xA7\xF0\x9F\x87\xB7"));
    if (screen.CellAt(0, 0).text.length() < 2 || screen.CursorColumn() != 2) return 20;

    std::cout << "terminal-screen-smoke: ok — VT, scrollback, mouse, paste, Unicode, links, sync, and graphics policy\n";
    return 0;
}
