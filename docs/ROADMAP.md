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

## Release 0.8.3 — advanced xterm compatibility

The `0.8.3` increment adds safe OSC 8 hyperlinks, DEC synchronized updates, broader grapheme joining for combining marks, variation selectors, ZWJ sequences, and regional-indicator pairs, word/line selection gestures, and a graphics policy that consumes Sixel/Kitty DCS/APC payloads without corrupting the terminal screen. Links are opened only for an explicit safe-scheme allowlist. Image rendering remains intentionally optional until a native rendering policy and resource limits are defined.

## Release 0.9 — debugging views and registry security

The completed `0.9.0` increment adds native breakpoint toggling and DAP `setBreakpoints`, thread enumeration, stack-trace, scopes, variables, evaluate, source mapping, persistent watches, clickable stack-frame locations, capability discovery, and automatic `configurationDone`, plus native debugging panels. Diagnostics can navigate back to editor locations. The extension installer computes SHA-256 digests, validates safe manifests, stages installations transactionally, rolls back an existing version if a commit fails, records installation metadata, and can require Ed25519 signatures when OpenSSL is available. The registry client can search Open VSX over HTTPS, cache catalog responses, and verify artifacts by digest. Workspace trust is persisted outside the source tree and gates task, terminal, debug-adapter, and VSIX execution for untrusted folders. Native Tree View, Git SCM, custom-editor registries, and per-extension compatibility reports are also available. Deeper contribution APIs and unattended download/install policy remain future work.

## Release 1.0 — classic native workbench and complete terminal compatibility

The 1.0 goal is a **classic, compact, and polished native IDE**. The visual direction keeps the direct project-tree, editor, build, debug, and terminal workflow that makes Code::Blocks practical, while adding modern navigation and feedback patterns inspired by Xcode. The result must remain recognizably Codium::Blocks: native C++/wxWidgets, keyboard-first, fast to start, and free from Electron in the core process.

The first 1.0 implementation increment now provides a native project navigator, a central tabbed editor, a dockable bottom workbench, Problems/Build/Terminal/Debug/Output pages, a status bar, a normalized compiler/LSP/terminal problem model, clickable problem navigation, and inline underlines for active diagnostics. The remaining 1.0 work is to deepen the editor gutter, source annotations, scheme bar, build parser, keyboard navigation, themes, and cross-platform visual verification.

### 1.0 interface

The default workbench will provide a compact menu and toolbar, a scheme bar for configuration/target/toolchain selection, a project navigator, a tabbed editor, an optional inspector, and a dockable bottom workbench. The bottom workbench will contain Problems, Build, Debug, Terminal, Output, and Tasks views. Every secondary region must be collapsible so that the editor remains the visual center of the application.

The visual system will offer refined light and dark themes, readable native controls, restrained accent colors, compact spacing, high-contrast support, platform font scaling, and icons or text labels in addition to color. The interface will improve the classic Code::Blocks layout without copying a proprietary product identity.

### 1.0 warnings, errors, and source feedback

Compiler output, language-server diagnostics, task output, terminal diagnostics, and debug-adapter messages will feed a normalized problem model. Reliable locations will appear simultaneously in raw output, the Problems view, and the source editor. Errors will have red gutter markers and underlines. Warnings will have amber markers and underlines. Information and hints will use less intrusive markers.

Selecting a diagnostic will show its full message, source, code, range, related locations, and available quick actions without changing the document text. Double-clicking a problem or a recognized `path:line:column` terminal message will open the source file and move the caret to the correct location. The Problems view will provide severity/source/file filters, counts, next/previous navigation, stale-result state, and access to the originating raw output.

The Build view will retain structured sessions with raw output, elapsed time, exit status, parsed warnings and errors, and a rerun action. Parsing will be conservative for GCC/Clang, MSVC, Rust, and language-server formats. Unrecognized lines will remain raw output instead of becoming false diagnostics. See [`docs/PROBLEMS.md`](PROBLEMS.md) for the diagnostic contract.

### 1.0 terminal and debug workflow

The terminal will retain PTY/ConPTY interactivity, ANSI and cursor behavior, scrollback, selection, hyperlinks, bracketed paste, mouse reporting, and keyboard forwarding. Recognized source locations in terminal output will be clickable without changing the child process's input or output. Failed commands will show their exit status in the terminal header and status bar.

The Debug view will use the same visual language as Problems and Build. Breakpoints will appear in the editor gutter and list. Stack frames will be clickable. Variables, watches, threads, scopes, and the debug console will remain dockable. Unsupported adapter capabilities will be visible as disabled actions with explanations.

### 1.0 navigation and productivity

The command palette, go-to-file, go-to-symbol, go-to-line, next/previous problem, focus-terminal, and build-and-run actions will have visible shortcuts. The primary workflow must be usable with the keyboard and must not depend on hidden mouse-only controls. The interface will preserve direct double-click navigation for files, problems, stack frames, and terminal locations.

### 1.0 terminal compatibility and optional media

The terminal compatibility layer will add mathematically complete Unicode grapheme breaking and width handling, including difficult combining, emoji, and wide-character cases. Sixel and Kitty image rendering may be enabled through optional backends with explicit resource limits, image lifetime management, and security policy. Image and web rendering backends must remain optional and must not load during IDE startup.

### 1.0 contribution and platform scope

The native Tree View, SCM, and custom-editor registries will be deepened into versioned contribution contracts. Optional webviews will be isolated from the native startup path and subject to workspace trust and resource policies. The release must be validated on Windows, macOS, and Linux with high-DPI scaling, high-contrast settings, keyboard-only navigation, clean builds, failed builds, language-server diagnostics, terminal diagnostics, debugging, and workspace trust.

The complete interface specification is maintained in [`docs/UI_DESIGN.md`](UI_DESIGN.md). The 1.0 release must satisfy the acceptance criteria in that document and in [`docs/PROBLEMS.md`](PROBLEMS.md).

## Compatibility criteria

Every release must publish a tested matrix using real open-source extensions. Compatibility means that a package installs, activates, registers its contributions, and completes its primary scenarios. The project must not claim full compatibility while private APIs, webviews, or proprietary dependencies remain unsupported.
