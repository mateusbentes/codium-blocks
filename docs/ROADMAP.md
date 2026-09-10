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

The first 1.0 implementation increment now provides a native project navigator, a central tabbed editor with a real line-number gutter, a dockable bottom workbench, Problems/Build/Terminal/Debug/Output pages, a status bar, a normalized compiler/LSP/terminal problem model, clickable problem navigation, severity markers and inline underlines for active diagnostics, a Debug/Release scheme bar with target/toolchain selection, conservative GCC/Clang, MSVC, Rust, and ANSI Build parsing, a safe Code::Blocks SDK bridge for header, plugin-directory, native-library, and manifest discovery, a `.cbp` target importer, a versioned host-adapter event contract, and an optional external adapter client with JSON Lines handshake, capability discovery, structured build events, and native Problems integration. The `1.0.1` increment adds severity/source filters, circular next/previous problem navigation with `F8` and `Shift+F8`, rerun of the last Build or Configure task with `Ctrl+Shift+B`, and the first unified target/scheme/Build-session workflow described in Phase F. The staged Code::Blocks host adapter is now implemented through the matched SDK Compiler and Debugger boundaries, with the private DebuggerGDB provider and native snapshot panels kept opt-in. The remaining 1.0 work is to deepen source annotations, additional keyboard navigation, themes, cross-platform visual verification, richer generator semantics, variable expansion, and broader artifact validation. User-defined tasks and schemes, generator-aware artifact discovery, and explicit build-then-run orchestration are implemented in the first Phase F follow-up increment.

### 1.0 interface

The default workbench will provide a compact menu and toolbar, a scheme bar for configuration/target/toolchain selection, a project navigator, a tabbed editor, an optional inspector, and a dockable bottom workbench. The bottom workbench will contain Problems, Build, Debug, Terminal, Output, and Tasks views. Every secondary region must be collapsible so that the editor remains the visual center of the application.

The visual system will offer refined light and dark themes, readable native controls, restrained accent colors, compact spacing, high-contrast support, platform font scaling, and icons or text labels in addition to color. The interface will improve the classic Code::Blocks layout without copying a proprietary product identity.

### 1.0 warnings, errors, and source feedback

Compiler output, language-server diagnostics, task output, terminal diagnostics, and debug-adapter messages will feed a normalized problem model. Reliable locations will appear simultaneously in raw output, the Problems view, and the source editor. Errors will have red gutter markers and underlines. Warnings will have amber markers and underlines. Information and hints will use less intrusive markers.

Selecting a diagnostic will show its full message, source, code, range, related locations, and available quick actions without changing the document text. Double-clicking a problem or a recognized `path:line:column` terminal message will open the source file and move the caret to the correct location. The Problems view provides severity/source filters, counts, circular next/previous navigation, stale-result state, and access to the originating raw output. `F8` selects the next visible problem and `Shift+F8` selects the previous one; navigation wraps at either end so keyboard users can inspect a filtered list continuously.

The Build view retains structured sessions with raw output, elapsed time, exit status, parsed warnings and errors, and a rerun action. The last Build or Configure task is preserved with its selected scheme arguments and can be rerun from the Build menu, Problems panel, command palette, or `Ctrl+Shift+B`. The current parser handles GCC/Clang, MSVC, ANSI-prefixed output, and Rust's header-plus-location format; unrecognized lines remain raw output instead of becoming false diagnostics. The scheme bar selects Debug or Release configuration, a target, and the detected toolchain, and CMake Build/Configure tasks receive the selected configuration. See [`docs/PROBLEMS.md`](PROBLEMS.md) for the diagnostic contract.

### Phase F — unified Build, targets, schemes, and execution

The first Phase F increment is implemented as a continuation of the 1.0.1 work. CMake, Make, Cargo, npm, Ninja, CMake Presets, and imported Code::Blocks targets now share one value-owned target model. Target-specific Build tasks are generated where the project format exposes enough information, and the scheme bar persists Debug/Release, target, and toolchain choices per workspace. The active scheme is used by Build, Run target, DAP launch defaults, and the isolated Code::Blocks adapter target selection.

Build and Configure actions now create persistent sessions containing task metadata, target, configuration, toolchain, elapsed time, exit status, cancellation state, and raw output. The Build panel restores recent sessions, while parsed Problems retain the originating session identifier. Diagnostic normalization now covers CMake, Ninja, Make, linker, GCC/Clang, MSVC, Rust, and ANSI-prefixed output without converting ordinary tool progress into false Problems. The Phase F follow-up adds `.codium-blocks/tasks.tsv` and `.codium-blocks/schemes.tsv`, CMake Presets and conservative Ninja discovery, generator-aware artifact candidates, and explicit Build-and-Run sequencing. The process boundary remains an executable plus an argument vector; it does not create an implicit shell.

### Phase G — native editor productivity

The first Phase G increment adds a reusable native editor-actions model and connects it to the wxWidgets editor. Find, Find next, Find previous, Replace, Replace all, and Go to Line are available from the Edit menu, command palette, and visible keyboard shortcuts. Search supports case-sensitive and case-insensitive matching at the model boundary, wraps when navigation reaches either end of the document, and preserves the existing UTF-8 document buffer. The editor retains native undo behavior through `wxTextCtrl`, while replacements continue to update dirty state, the window title, and the active language-server document.

The current Phase G editor increment adds a value-owned, conservative native tokenizer and color styling for common C/C++, Python, Rust, Go, Java, JavaScript, TypeScript, JSON, HTML, CSS, Markdown, YAML, and CMake constructs. It highlights comments, strings, numbers, keywords, types, functions, properties, preprocessor lines, markup tags, and Markdown headings without loading a browser or requiring an extension. HTML `<script>` and `<style>` regions are delegated to bounded JavaScript and CSS tokenization, while ordinary markup remains native. The LSP client now advertises semantic-token capabilities, reads the server legend, requests `textDocument/semanticTokens/full`, decodes the standard delta-encoded data, and composes server-provided styles over the lexical fallback. The current increment also adds Go to File, document/workspace symbol result navigation, definition, declaration, references, rename and code-action requests, completion text-edit application, multi-block hover presentation, delimiter matching, paired delimiters, basic indentation, and circular tab navigation. This remains a layered editor model rather than a universal parser: the language server owns semantic analysis, and unsupported or unavailable semantic tokens fall back safely to lexical highlighting. Real-server validation matrices for clangd, rust-analyzer, gopls, and pyright, richer hover/completion interaction, code-action context, symbol indexing, and advanced navigation remain future work.

### 1.0 terminal and debug workflow

The terminal will retain PTY/ConPTY interactivity, ANSI and cursor behavior, scrollback, selection, hyperlinks, bracketed paste, mouse reporting, and keyboard forwarding. Recognized source locations in terminal output will be clickable without changing the child process's input or output. Failed commands will show their exit status in the terminal header and status bar.

The Debug view will use the same visual language as Problems and Build. Breakpoints will appear in the editor gutter and list. Stack frames will be clickable. Variables, watches, threads, scopes, and the debug console will remain dockable. Unsupported adapter capabilities will be visible as disabled actions with explanations. The Code::Blocks adapter now feeds its value-owned provider snapshots into these native panels; richer frame selection and live-refresh scheduling remain follow-up work.

### 1.0 navigation and productivity

The command palette, go-to-file, go-to-symbol, go-to-line, next/previous problem, focus-terminal, and build-and-run actions will have visible shortcuts. The primary workflow must be usable with the keyboard and must not depend on hidden mouse-only controls. The interface will preserve direct double-click navigation for files, problems, stack frames, and terminal locations.

### 1.0 terminal compatibility and optional media

The terminal compatibility layer will add mathematically complete Unicode grapheme breaking and width handling, including difficult combining, emoji, and wide-character cases. Sixel and Kitty image rendering may be enabled through optional backends with explicit resource limits, image lifetime management, and security policy. Image and web rendering backends must remain optional and must not load during IDE startup.

### 1.0 contribution and platform scope

The native Tree View, SCM, and custom-editor registries will be deepened into versioned contribution contracts. Optional webviews will be isolated from the native startup path and subject to workspace trust and resource policies. The release must be validated on Windows, macOS, and Linux with high-DPI scaling, high-contrast settings, keyboard-only navigation, clean builds, failed builds, language-server diagnostics, terminal diagnostics, debugging, and workspace trust.

The complete interface specification is maintained in [`docs/UI_DESIGN.md`](UI_DESIGN.md). The 1.0 release must satisfy the acceptance criteria in that document and in [`docs/PROBLEMS.md`](PROBLEMS.md).

### Code::Blocks host boundary

The bridge remains discovery-only in the main process. Code::Blocks plugins depend on `Manager`, `PluginManager`, `cbPlugin`, event infrastructure, resources, and a matching SDK ABI; they must not be loaded as arbitrary shared libraries. Codium::Blocks imports `.cbp` targets and defines the additive versioned 1.3 event contract used by the isolated adapter, while retaining compatibility with earlier 1.0–1.2 clients.

The first production adapter increment is now implemented as an optional, separate `codium-blocks-codeblocks-adapter` executable. Its Phase A bootstrap owns a wxWidgets application and hidden frame, loads the official Code::Blocks resources, loads only an explicitly selected SDK-matched `Compiler` plugin, calls the real `ProjectManager::LoadProject()`, and emits `projectOpened` plus one structured `projectTarget` event per real `cbProject` target. It reports SDK identity, resources, compiler-plugin matching, and target-enumeration capabilities. Phase B registers official SDK event sinks and normalizes project lifecycle/file events plus `cbEVT_COMPILER_STARTED` and `cbEVT_COMPILER_FINISHED` into value-owned JSON Lines events. Phase C now invokes the matched compiler for real target builds, forwards PipedProcess stdout/stderr as `compilerOutput`, preserves completion status, and explicitly unloads the plugin before manager teardown. Phase D adds an optional matched Debugger plugin through a private allowlisted staging directory, supplies headless DebuggerManager interfaces inside the adapter process, exposes debug launch/control, and normalizes official debugger lifecycle events. Linux integration smokes verify `.cbp` enumeration, event normalization, a compiler warning, a produced executable, and debugger events when the SDK, matched plugins, and Xvfb are installed; the portable fake-adapter smoke test remains universal.

Phase E.1 now provides a safe debugger data boundary: a value-owned public state snapshot with running/stopped/busy state, exit code, active frame, current source location, breakpoint count, and debugger feature flags. Phase E.2 adds an opt-in provider shared library compiled from an exact DebuggerGDB source revision and ABI tuple. After identity validation, it serializes frames, threads, breakpoints, watches, and expression-backed variables as value-owned JSON, including source file and line text where the provider supplies them. Phase E.3 parses those snapshots into native view models and updates the Debug workbench's call stack, threads, breakpoints, watches, variables, and source-mapped locations without linking private provider headers into the main process. The generic adapter remains safe and returns `debugDataUnavailable` when the provider is absent. Full Code::Blocks debugger/plugin compatibility is not implied. See [`docs/CODEBLOCKS_INTEGRATION.md`](CODEBLOCKS_INTEGRATION.md) and [`docs/CODEBLOCKS_ADAPTER_PROTOCOL.md`](CODEBLOCKS_ADAPTER_PROTOCOL.md).

## Compatibility criteria

Every release must publish a tested matrix using real open-source extensions. Compatibility means that a package installs, activates, registers its contributions, and completes its primary scenarios. The project must not claim full compatibility while private APIs, webviews, or proprietary dependencies remain unsupported.
