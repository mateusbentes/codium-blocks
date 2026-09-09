# Interactive Terminal

Codium::Blocks 0.8.0 uses a real interactive terminal backend instead of ordinary pipes whenever the platform provides one. Linux and macOS use a pseudo-terminal (PTY); Windows uses ConPTY dynamically when the operating system exposes the API. If ConPTY is unavailable, the implementation reports a pipe fallback instead of silently claiming full terminal compatibility.

The terminal starts in the active workspace directory and supports shell selection through the native session API. The default shell is `/bin/sh -i` on Unix-like systems and `cmd.exe /Q` on Windows. The architecture does not depend on Alacritty, Kitty, Konsole, GNOME Terminal, Windows Terminal, or another external emulator.

The native terminal now propagates resize requests, preserves UTF-8 and raw ANSI sequences, renders common ANSI colors in the terminal panel, stores command history, and maps Up/Down to history navigation. PTY/ConPTY is required for applications that inspect terminal capabilities or use cursor control, although full terminal emulation remains a future enhancement.

The transport test validates interactive echo, resize, ANSI preservation, clean shutdown, and DAP independently using fake child processes. This keeps the test deterministic and does not require a graphical terminal emulator or a user login shell.
