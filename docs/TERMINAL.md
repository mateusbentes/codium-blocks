# Interactive Terminal and VT Screen

Codium::Blocks uses a real interactive terminal backend and a native VT screen model. Linux and macOS use a pseudo-terminal (PTY); Windows uses ConPTY dynamically when the operating system exposes the API. If ConPTY is unavailable, the implementation reports a pipe fallback instead of silently claiming full terminal compatibility.

The terminal starts in the active workspace directory and supports shell selection through the native session API. The default shell is `/bin/sh -i` on Unix-like systems and `cmd.exe /Q` on Windows. The architecture does not depend on Alacritty, Kitty, Konsole, GNOME Terminal, Windows Terminal, or another external emulator.

The VT screen model maintains a grid of cells, cursor position, foreground colors, bold and underline attributes, scroll behavior, erase operations, save/restore cursor, cursor visibility, wrap mode, and the alternate screen used by Vim, Neovim, less, and many curses applications. It understands common CSI cursor movement, SGR colors, erase, insert/delete, DEC private modes, and OSC title sequences. The UI renders the model as a native wxWidgets terminal surface instead of appending plain log text.

Keyboard input is sent directly to the PTY/ConPTY. The first interactive key layer covers UTF-8 text, Enter, Escape, Tab, Backspace, arrow keys, Home/End, Insert/Delete, PageUp/PageDown, Ctrl-letter control bytes, and Alt-prefixed input. Resize events are propagated to both the operating-system terminal backend and the VT screen dimensions.

The terminal-screen smoke test validates cursor movement, SGR colors, erase-display, scrolling, alternate-screen entry/exit, and cursor visibility. The transport test separately validates interactive echo, resize, ANSI preservation, clean shutdown, and DAP. These tests do not require a graphical terminal emulator or a user login shell.

This is a focused VT implementation rather than a complete xterm emulator. Future work includes richer true-color/background rendering, mouse reporting, bracketed paste, hyperlink OSC sequences, scrollback, selection, synchronized updates, full wide-character/combining-cell support, and persistent terminal profiles. Those features are isolated from the PTY and DAP transports.
