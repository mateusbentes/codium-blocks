# Interactive Terminal and VT Screen

Codium::Blocks 0.8.2 combines a real interactive PTY/ConPTY backend with a native VT screen model. Linux and macOS use a pseudo-terminal (PTY); Windows uses ConPTY dynamically when the operating system exposes the API. If ConPTY is unavailable, the implementation reports a pipe fallback instead of silently claiming full terminal compatibility.

The screen model maintains cells, cursor position, foreground/background attributes, bold and underline state, erase operations, scrolling, save/restore cursor, cursor visibility, wrap mode, and the alternate screen used by Vim, Neovim, less, and curses applications. It understands common CSI cursor movement, SGR colors, DEC private modes, OSC termination, mouse-reporting modes, and bracketed-paste mode.

The terminal now has a bounded scrollback buffer and mouse-wheel viewport navigation. When the child requests mouse reporting, clicks, releases, motion-independent wheel events, and wheel direction are encoded in legacy or SGR mouse format. When mouse reporting is disabled, the same wheel is used for local scrollback navigation. The panel supports drag selection and clipboard copy.

Keyboard input is sent directly to the PTY/ConPTY. The input layer covers UTF-8 text, Enter, Escape, Tab, Backspace, arrow keys, Home/End, Insert/Delete, PageUp/PageDown, Ctrl-letter control bytes, and Alt-prefixed input. Clipboard paste is wrapped in bracketed-paste markers whenever the child application enables mode 2004.

The first Unicode layer recognizes common combining-mark ranges and wide East Asian/emoji ranges. Wide cells reserve a continuation cell so cursor movement and selection remain aligned for CJK text and many emoji. Full grapheme clustering and every Unicode width edge case remain outside this increment.

Terminal profiles are stored outside the source tree through `wxFileConfig`: `%APPDATA%/CodiumBlocks` on Windows, `~/Library/Application Support/CodiumBlocks` on macOS, and `$XDG_STATE_HOME/codium-blocks` or `~/.config/codium-blocks` on Linux. `CODIUM_BLOCKS_DATA` overrides the location for portable deployments and tests. The default profile stores the selected shell, dimensions, and the last 500 command-history entries.

The terminal-screen smoke test validates cursor movement, SGR colors, erase-display, scrolling, alternate-screen entry/exit, cursor visibility, scrollback, mouse modes, bracketed paste, wide cells, and combining characters. The profile smoke test validates persistence. The transport test separately validates interactive echo, resize, ANSI preservation, clean shutdown, and DAP. These tests do not require a graphical terminal emulator or a user login shell.

This is a focused native VT implementation rather than a complete xterm emulator. Future work includes richer hyperlink OSC sequences, synchronized updates, full grapheme clustering, sixel/kitty graphics policy, and more advanced selection semantics.
