# Interactive Terminal and VT Screen

Codium::Blocks 0.8.3 combines a real interactive PTY/ConPTY backend with a native VT screen model. Linux and macOS use a pseudo-terminal (PTY); Windows uses ConPTY dynamically when the operating system exposes the API. If ConPTY is unavailable, the implementation reports a pipe fallback instead of silently claiming full terminal compatibility.

The screen model maintains cells, cursor position, foreground/background attributes, bold and underline state, erase operations, scrolling, save/restore cursor, cursor visibility, wrap mode, and the alternate screen used by Vim, Neovim, less, and curses applications. It understands common CSI cursor movement, SGR colors, DEC private modes, OSC termination, mouse-reporting modes, and bracketed-paste mode.

The advanced xterm layer recognizes OSC 8 hyperlinks. A hyperlink is stored on each affected cell and rendered in a native underlined style. Clicking a hyperlink opens it only when its scheme is `http`, `https`, `mailto`, or `file`; unsupported schemes are discarded. This explicit allowlist prevents terminal output from launching arbitrary protocols.

DEC synchronized updates (`CSI ? 2026 h/l`) suppress intermediate native repaints until the application ends the batch. This avoids visible tearing during full-screen redraws from Vim, Neovim, and curses applications while preserving the child process's output order.

The terminal has a bounded scrollback buffer and mouse-wheel viewport navigation. When the child requests mouse reporting, clicks, releases, motion, and wheel events are encoded in legacy or SGR mouse format. When mouse reporting is disabled, the same wheel is used for local scrollback navigation. The panel supports drag selection, double-click word selection, triple-click line selection, and clipboard copy.

Keyboard input is sent directly to the PTY/ConPTY. The input layer covers UTF-8 text, Enter, Escape, Tab, Backspace, arrow keys, Home/End, Insert/Delete, PageUp/PageDown, Ctrl-letter control bytes, and Alt-prefixed input. Clipboard paste is wrapped in bracketed-paste markers whenever the child application enables mode 2004.

The Unicode layer recognizes common combining-mark and variation-selector ranges, ZWJ joins, regional-indicator pairs, and wide East Asian/emoji ranges. Wide cells reserve a continuation cell so cursor movement and selection remain aligned for CJK text and many emoji. This is a practical grapheme approximation, not a complete implementation of every Unicode grapheme-break and width rule.

Sixel DCS and Kitty graphics APC payloads are consumed and discarded by policy rather than written into the visible screen. This prevents binary image payloads from corrupting terminal text. Optional native rendering for those protocols is scheduled for release 1.0 and will require explicit resource limits, image lifetime management, and a security policy.

Terminal profiles are stored outside the source tree through `wxFileConfig`: `%APPDATA%/CodiumBlocks` on Windows, `~/Library/Application Support/CodiumBlocks` on macOS, and `$XDG_STATE_HOME/codium-blocks` or `~/.config/codium-blocks` on Linux. `CODIUM_BLOCKS_DATA` overrides the location for portable deployments and tests. The default profile stores the selected shell, dimensions, and the last 500 command-history entries.

On Unix, stopping a PTY session signals its process group and uses bounded escalation to avoid leaving descendant processes behind or blocking the workbench indefinitely when a shell ignores termination signals. Windows ConPTY uses the corresponding process termination path provided by the operating system.

The terminal-screen smoke test validates cursor movement, SGR colors, erase-display, scrolling, alternate-screen entry/exit, cursor visibility, scrollback, mouse modes, bracketed paste, wide cells, combining characters, OSC 8 allowlisting, synchronized updates, graphics payload consumption, and regional-indicator pairing. The profile smoke test validates persistence. The transport test separately validates interactive echo, resize, ANSI preservation, clean shutdown, and DAP.

This remains a focused native VT implementation rather than a complete xterm emulator. Release 1.0 is the target for complete Unicode grapheme segmentation, full Unicode width edge cases, and an optional, resource-limited Sixel/Kitty image-rendering policy. Later work may refine synchronized-update edge cases and richer hyperlink behavior.
