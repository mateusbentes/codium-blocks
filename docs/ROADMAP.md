# Codium::Blocks Implementation Roadmap

## Scope and release policy

Codium::Blocks is developed as a native C++17 and wxWidgets IDE for Windows, macOS, and Linux. The core process remains independent of Electron, Chromium, and the Code::Blocks SDK. JavaScript and TypeScript extensions run in an optional out-of-process Node.js host. Code::Blocks integration runs in a separate native adapter process and is enabled only when a matched SDK, runtime, plugin set, and ABI identity are available.

The roadmap describes verified capabilities rather than aspirations presented as completed work. Each delivery must preserve the portable build, retain an adapter-disabled path, add deterministic tests for new value-owned models, update the English documentation, and pass the available cross-platform CI matrix. Optional integrations are reported as unavailable when their external toolchains are not installed; they are never replaced with fabricated success.

## Verified foundation

The project already contains the architectural foundation required for a lightweight native IDE. The native workbench provides a project navigator, tabbed documents, a line-number gutter, a dockable bottom workbench, Problems, Build, Terminal, Debug, Output, and Tasks views, a command palette, workspace trust, and a persistent scheme bar. The document model supports UTF-8 text, dirty state, saving, multiple buffers, and workspace-relative paths.

The build system provides CMake, Make, Cargo, npm, Ninja, CMake Presets, and Code::Blocks target discovery through a common target model. User-defined tasks and schemes use executable argument vectors without implicit shell execution. Build sessions preserve task metadata, selected target, configuration, toolchain, elapsed time, status, output, and diagnostics. Build and Run use the selected scheme and discovered artifact candidates.

The editor provides lexical highlighting, LSP initialization and document synchronization, semantic-token decoding, Go to File, document and workspace symbol navigation, definition and declaration requests, references, completion text-edit application, multi-block hover presentation, rename, code actions, delimiter matching, paired delimiters, basic indentation, Find and Replace, Go to Line, and circular tab navigation. The LSP transport is exercised by a deterministic fake server and by an optional real-server harness for clangd, rust-analyzer, gopls, and pyright-langserver.

The terminal provides PTY support on Linux and macOS, dynamically selected ConPTY support on Windows with a pipe fallback, ANSI and VT handling, resize propagation, scrollback, selection, bracketed paste, mouse reporting, terminal profiles, safe hyperlinks, synchronized updates, and bounded graphics-payload consumption. The remaining terminal limitations are documented separately and do not affect native startup.

The debugger foundation provides DAP framing, adapter lifecycle controls, session states, automatic refresh after stopped events, clearing of stale views after continued or terminated events, threads, stack frames, scopes, variables, watches, source mapping, and a native Debug panel. The current public debugger delivery also synchronizes the selected frame with the editor, persists breakpoints per workspace, and sends conditional breakpoints, hit conditions, and logpoints when the adapter advertises or accepts those DAP fields. Adapter responses remain correlated to their source request and are represented as value-owned pending, verified, or rejected states.

The Code::Blocks boundary is implemented as an optional isolated process. The public SDK path discovers projects, normalizes official project and compiler events, builds matched targets, captures compiler output, and exposes debugger lifecycle information. The private DebuggerGDB provider is separately compiled from an exact source and ABI identity. Its snapshots are transferred as value-owned data, and the main process never loads private Code::Blocks plugin libraries.

## Ordered delivery plan

### 1. Debugger and editor synchronization

This delivery is implemented in the current public debugger increment. When a stopped event arrives, the native client refreshes the thread list and stack trace, selects the adapter's active frame, maps its source path, opens the corresponding document when it is available, moves the caret to the reported line and column, and displays the frame location in the Debug status. Selecting another frame repeats the same operation and refreshes its scopes. Continued, terminated, exited, and disconnected events clear transient variables, stack frames, and frame-location state so the editor does not present stale execution data.

The implementation remains conservative. A frame whose source path cannot be mapped or whose file is unavailable remains visible in the call stack, but it does not replace the active editor document. The generic DAP path does not infer private adapter data and does not claim complete GDB or LLDB compatibility.

### 2. Breakpoint management

Workspace breakpoints are stored outside the source tree under the configured Codium::Blocks data directory. The persistence format is escaped UTF-8 TSV and is replaced atomically. Each entry records the source path, requested line, conditional expression, hit-count expression, and optional log message. Loading a workspace restores the requested breakpoint set before a debug adapter is started.

The Debug panel and editor gutter distinguish pending, verified, and rejected states. Toggling a breakpoint updates persistence and sends a source-specific `setBreakpoints` request when an adapter is running. Configuring a breakpoint updates its condition, hit condition, or log message and resends the complete source request. A disabled state remains reserved for a future adapter capability because DAP does not define one universal disable operation independent of removing a breakpoint from the requested set.

The next debugger refinement is real-session validation with GDB and LLDB adapters, followed by adapter capability gating, breakpoint persistence migration, conditional-breakpoint diagnostics, logpoint output presentation, function breakpoints, and data breakpoints where the adapter explicitly supports them.

### 3. Reproducible language-server validation

The current real-server harness creates isolated, language-appropriate temporary workspaces and safely skips a server that is unavailable or cannot start before protocol initialization. A release-quality matrix must additionally provide pinned server versions and representative fixtures for clangd, rust-analyzer, gopls, and pyright on supported runners. After startup, failures in initialization, document synchronization, diagnostics, navigation, completion, hover, rename, or shutdown must fail the corresponding job rather than being classified as environmental skips.

The matrix will remain independent of the portable core. Installing a language server will be a CI concern or an explicit developer choice, not a runtime dependency of the native IDE. The matrix will report the exact executable, version, fixture, protocol scenario, and skip reason in its job summary.

### 4. Themes, accessibility, and visual verification

The native workbench will provide coordinated light and dark themes with a shared information hierarchy. Theme tokens will cover editor text, selections, current line, comments, strings, semantic tokens, Problems severities, breakpoints, terminal cells, panels, status indicators, and disabled controls. Colors will not be the only state signal; labels, icons, patterns, and accessible names will remain available.

Accessibility work will cover keyboard-only navigation, focus order, native control labels, high-contrast system settings, font scaling, reduced visual density, and non-color descriptions for diagnostics and breakpoint states. Visual validation will exercise Linux, macOS, and Windows at ordinary and high-DPI scales, with screenshots or structured inspection used to catch clipped controls, unreadable labels, focus loss, and incorrect gutter alignment.

### 5. Native packaging

Packaging will produce native distributable artifacts without changing the runtime architecture. Linux will provide a relocatable archive and a distribution-friendly package recipe. macOS will provide an application bundle with an explicit resource layout and architecture policy. Windows will provide a native application bundle with the required wxWidgets runtime files and a documented installer or portable archive path.

Packaging tests will verify that a clean machine can launch the application, locate resources, preserve the optional Node.js host boundary, and keep Code::Blocks adapter files separate from the main executable. Signing, notarization, and certificate policy will remain explicit release concerns rather than being implied by an unsigned development archive.

### 6. Extension API depth

The extension host will deepen versioned contracts for commands, configuration, language contributions, Tree Views, SCM providers, custom editors, diagnostics, and workspace events. Native features will remain responsible for editor, project, compiler, terminal, debugger, and operating-system integration. The Node.js host will not receive arbitrary native pointers or private Code::Blocks objects.

Every added API must define activation behavior, failure handling, trust requirements, process ownership, compatibility reporting, and a deterministic smoke scenario. Webviews and image-rendering backends remain optional and must not load during native startup. The project will continue to report tested compatibility rather than claiming complete VS Code compatibility.

## Release 1.0 acceptance criteria

Release 1.0 requires a stable classic layout, a fast native editor, navigable Problems, structured Build sessions, a reliable PTY or ConPTY terminal, and a tested Debug workflow on all three target operating systems. A compiler, terminal, or language-server diagnostic with a reliable location must remain visible in its originating output and become a clickable source annotation.

| Area | Required evidence |
|---|---|
| Native workbench | Clean startup, compact layout, visible menus and shortcuts, collapsible secondary regions, and no Electron or Chromium dependency in the core process. |
| Editor | UTF-8 editing, undo, save, lexical fallback highlighting, semantic-token composition, navigation, completion application, hover, rename, code actions, delimiter handling, and keyboard-first operation. |
| Build | Portable target discovery, user tasks and schemes, scheme-aware Build and Run, persistent sessions, conservative diagnostics, and no implicit shell execution. |
| Problems | Correct severity, source, range, stale state, filtering, gutter rendering, raw-output correlation, and navigation for compiler, terminal, LSP, and adapter diagnostics. |
| Terminal | Interactive PTY or ConPTY behavior, ANSI/VT rendering, resize, scrollback, selection, mouse reporting, bracketed paste, profiles, and safe handling of unsupported graphics payloads. |
| Debug | DAP lifecycle, automatic stopped-event refresh, active-frame editor synchronization, source mapping, watches, variables, breakpoint states, and explicit unsupported-capability reporting. |
| Code::Blocks | Portable adapter-disabled build, matched-SDK adapter smoke, isolated plugin loading, value-owned events, and no foreign plugin loading in the main process. |
| Platforms | Successful clean builds and smoke tests on `ubuntu-24.04`, `macos-15`, and `windows-2022`, with separate optional Linux SDK/provider jobs. |
| Security | Workspace trust gates execution, extension and adapter boundaries remain explicit, installation is transactional, and third-party native compatibility is not overstated. |

## Explicit non-goals and post-1.0 work

The project does not promise that every VS Code extension, every Code::Blocks plugin, or every Xcode workflow will work. Electron-dependent extensions, private VS Code APIs, proprietary services, arbitrary native plugins, unsupported webviews, and unmatched Code::Blocks binaries require explicit compatibility work or remain unsupported.

Post-1.0 work may add mathematically complete Unicode grapheme and width handling, optional Sixel or Kitty rendering with strict resource limits, deeper Tree View and SCM APIs, richer custom editors, unattended extension registry workflows, and broader debugger features. These capabilities will be introduced only when their native boundary, security policy, tests, and platform behavior are defined.

## References

[1]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/UI_DESIGN.md "Codium::Blocks native interface specification"
[2]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/PROBLEMS.md "Codium::Blocks diagnostics contract"
[3]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/CODEBLOCKS_INTEGRATION.md "Codium::Blocks Code::Blocks SDK integration"
[4]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/CODEBLOCKS_ADAPTER_PROTOCOL.md "Codium::Blocks adapter protocol"
[5]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/DEBUGGING.md "Codium::Blocks debugging model"
