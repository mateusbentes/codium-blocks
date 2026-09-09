# Implementation Roadmap

## Release 0.1 — proven foundation

This release proves the minimum architecture: a wxWidgets window, a separate Node.js process, a JSON Lines protocol, CommonJS extension loading, command execution, host-to-UI notifications, offline VSIX installation, and an automated smoke test.

## Release 0.2 — documents, configuration, and language-process foundation

The current `0.2.0` development increment adds a native UTF-8 document model with open/edit/save state, an editor surface, persistent Extension Host configuration, manifest-contributed commands reflected in the native UI, and an LSP process manager using standard `Content-Length` framing. `clangd` can be started through the host when installed on the system.

## Release 0.3 — functional LSP client

The `0.3.0` increment adds the `initialize`/`initialized` handshake, document open/change synchronization, hover and completion requests, normalized diagnostics and result events, and a deterministic fake-LSP integration test. The native editor exposes actions for starting clangd, initializing LSP, requesting hover, and requesting completion.

## Release 0.4 — native LSP presentation

The `0.4.0` increment adds File, Language, and Extensions menus, keyboard shortcuts, a diagnostics panel, a hover/result panel, a native completion list, and language identification for common programming languages. LSP data is now visible in the native UI instead of only being written to the log.

## Release 0.5 — project workspaces and command palette

The `0.5.0` increment adds native workspace folder opening, recursive file discovery with generated/dependency filtering, a file tree, multiple document tabs, independent buffers, workspace-relative paths, and a native command palette. The next increment will add project configuration, file watching, and richer declarative contributions.

## Release 0.6 — build tasks and project configuration

The `0.6.0` increment adds automatic CMake, Make, Cargo, and npm detection, built-in Configure/Build/Test tasks, structured executable argument vectors, asynchronous output capture, task cancellation, a native task list, and Build menu integration. The next increment will add explicit user task configuration, terminal support, and problem navigation.

## Release 0.7 — native terminal and DAP transport

The `0.7.0` increment adds an interactive native terminal, workspace-aware stdin/stdout/stderr, a DAP client with `Content-Length` framing, adapter controls for initialize/launch/continue/pause/disconnect, and deterministic fake terminal/DAP tests. The next increment will add source breakpoints, stack frames, variables, watches, and clickable debug locations.

## Release 0.8 — interactive PTY/ConPTY terminal

The `0.8.0` increment upgrades the pipe-based terminal foundation to a real interactive terminal. Linux and macOS now use a pseudo-terminal (PTY), while Windows dynamically uses ConPTY when available and reports a pipe fallback otherwise. The terminal supports the native shell session API, resize propagation, ANSI color rendering, command history, keyboard shortcuts, UTF-8 input/output, and explicit backend capability reporting. The native terminal remains independent of Alacritty, Kitty, Konsole, Windows Terminal, and other external emulators.

The transport test covers shell startup, interactive echo, resize requests, ANSI sequence preservation, and clean shutdown without requiring a graphical terminal emulator. Full terminal emulation, cursor movement, mouse reporting, and richer shell configuration remain future work.

## Release 0.8.1 — native VT screen and interactive keyboard

The `0.8.1` increment adds a native VT screen model with cells, cursor movement, SGR attributes, erase and scroll operations, alternate-screen support, cursor visibility, wrap mode, direct keyboard forwarding, and a deterministic screen test. This is the layer required for Vim, Neovim, GDB's TUI, SSH, `top`, `htop`, and curses applications to interact with the IDE surface rather than only emitting log text. Future terminal work includes mouse reporting, scrollback, selection, bracketed paste, hyperlinks, synchronized updates, and complete wide-character support.

## Release 0.8.2 — terminal interaction and profiles

The `0.8.2` increment adds bounded scrollback with viewport navigation, drag selection and clipboard copy, legacy and SGR mouse reporting, bracketed paste, initial wide-Unicode and combining-character cells, and persistent terminal profiles for shell, dimensions, and command history. The terminal remains independent of external emulators. Future work is limited to richer xterm compatibility such as hyperlinks, synchronized updates, complete grapheme clustering, and advanced mouse/selection behavior.

## Release 0.9 — debugging views and registry security

Add source breakpoints, stack frames, variables, watches, problem navigation, Open VSX, private registries, caching, checksums, rollback, workspace trust, permissions, and per-extension compatibility reports.

## Release 1.0 — optional advanced UI

Add Tree Views, SCM, webviews, and custom editors. The web engine must remain optional and must not be loaded during IDE startup.

## Compatibility criteria

Every release must publish a tested matrix using real open-source extensions. Compatibility means that a package installs, activates, registers its contributions, and completes its primary scenarios. The project must not claim full compatibility while private APIs, webviews, or proprietary dependencies remain unsupported.
