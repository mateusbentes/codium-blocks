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

    std::cout << "terminal-screen-smoke: ok — VT cursor, colors, erase, scroll, and alternate screen\n";
    return 0;
}
