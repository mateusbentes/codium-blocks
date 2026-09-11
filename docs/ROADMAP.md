# Codium::Blocks Implementation Roadmap

## Scope and release policy

Codium::Blocks is developed as a native C++17 and wxWidgets IDE for Windows, macOS, and Linux. The core process remains independent of Electron, Chromium, and the Code::Blocks SDK. JavaScript and TypeScript extensions run in an optional out-of-process Node.js host. Code::Blocks integration runs in a separate native adapter process and is enabled only when a matched SDK, runtime, plugin set, and ABI identity are available.

This roadmap describes verified capabilities and bounded follow-up work. It does not present untested integrations as complete. Each delivery preserves the portable adapter-disabled build, adds deterministic tests for new value-owned models, updates the English documentation, and reports unavailable external toolchains instead of fabricating success.

## Verified product foundation

The native workbench provides a project navigator, tabbed documents, a line-number gutter, a dockable bottom workbench, Problems, Build, Terminal, Debug, Output, and Tasks views, a command palette, workspace trust, and a persistent scheme bar. The document model supports UTF-8 text, dirty state, saving, multiple buffers, and workspace-relative paths.

The build system provides CMake, Make, Cargo, npm, Ninja, CMake Presets, and Code::Blocks target discovery through a common target model. User-defined tasks and schemes use executable argument vectors without implicit shell execution. Build sessions preserve task metadata, selected target, configuration, toolchain, elapsed time, status, output, and diagnostics. Build and Run use the selected scheme and discovered artifact candidates.

The editor provides lexical highlighting, semantic-token decoding, Go to File, document and workspace symbol navigation, definition and declaration requests, references, completion text-edit application, multi-block hover presentation, rename, code actions, delimiter matching, paired delimiters, basic indentation, Find and Replace, Go to Line, and circular tab navigation. A deterministic fake server and an optional real-server harness exercise the LSP transport.

The terminal provides PTY support on Unix-like systems, dynamically selected ConPTY support on Windows with a pipe fallback, ANSI and VT handling, resize propagation, scrollback, selection, bracketed paste, mouse reporting, terminal profiles, safe hyperlinks, synchronized updates, and bounded graphics-payload consumption. Remaining Unicode and image-rendering limitations are documented separately.

The debugger foundation provides DAP framing, adapter lifecycle controls, session states, automatic refresh after stopped events, clearing of stale views after continued or terminated events, threads, stack frames, scopes, variables, watches, source mapping, and a native Debug panel. The client persists source breakpoints per workspace and sends conditional, hit-count, and logpoint fields. It also sends function-breakpoint and data-breakpoint requests only when the adapter advertises the corresponding capabilities.

The Code::Blocks boundary is implemented as an optional isolated process. The public SDK path discovers projects, normalizes official project and compiler events, builds matched targets, captures compiler output, and exposes debugger lifecycle information. The private DebuggerGDB provider is separately compiled from an exact source and ABI identity. Its snapshots are transferred as value-owned data, and the main process never loads private Code::Blocks plugin libraries.

## Current delivery status

The current increment completes the quality outcomes below and adds the first native installation and packaging foundation. Their status is summarized below.

| Outcome | Implemented evidence | Deliberate boundary |
|---|---|---|
| Versioned real-tool matrices | `tests/real-lsp-matrix.json` fixes four language-server entries, tool versions, representative workspaces, and capability-gated scenarios. `tests/real-dap-matrix.json` fixes GDB and LLDB adapter entries by platform. `tests/integration-matrix-smoke.mjs` validates both schemas on every build. | Runtime probes remain safely skippable when an external executable is not installed. An installed tool with a mismatched version, or one that starts and fails a required protocol scenario, is a test failure. |
| Native themes | The native UI has System, Light, Dark, and High contrast palettes. The selected theme updates panels, editor defaults, terminal surfaces, syntax colors, gutter markers, and status text. Dedicated workflows capture a high-contrast workbench at a large runner display resolution. | Human comparison of screenshots and complete system high-contrast detection remain validation work. |
| Accessibility signals | Gutter markers expose letter glyphs in addition to color, controls use native wxWidgets focus behavior, the high-contrast palette provides strong text/background separation, and each platform workflow produces a visual validation artifact. | A full accessibility audit still needs platform-specific keyboard, font-scaling, screen-reader, focus-restoration, and high-DPI checks. |
| Extension API depth | The Node host now supports workspace document open/change/save events, `workspace.textDocuments`, `TreeItem`, Tree Data Providers, and native Tree View event forwarding. | SCM provider methods, custom-editor activation, webviews, and broader VS Code API compatibility remain bounded follow-up work. |
| Advanced DAP requests | The native client serializes `setFunctionBreakpoints` and `setDataBreakpoints`; configured entries persist per workspace, adapter rejection messages are retained, the fake DAP validates payloads, and the real probe runs launch/stop/stack/scopes/variables/continue/disconnect scenarios when capabilities permit. | Adapter-specific data-ID discovery, richer rejection UX, and broader LLDB/OpenDebugAD7 behavior remain validation work. |
| Platform CI isolation | Linux, macOS, Windows, and Code::Blocks Linux integration are separate workflow files. Each file mentions and executes only its own environment. Separate package workflows now stage Linux, macOS, and Windows artifacts with CPack and checksums. | Package artifacts are not signed, notarized, or published automatically. Clean-environment installation and human release review remain required. |

## Ordered follow-up work

### Reproducible language-server validation

The versioned matrix is now a required contract check. Dedicated platform workflows install or cache the pinned clangd, rust-analyzer, gopls, and pyright versions. The runtime harness creates isolated, language-appropriate workspaces and verifies initialize, document open, hover, completion, definition, declaration, references, document symbols, workspace symbols, and rename when the server advertises each provider. Because gopls creates its workspace view asynchronously, the harness explicitly announces the temporary workspace folder, gives it a short startup grace period, and allows a bounded retry for the explicit `no views` startup response on slower runners. Pyright receives a bounded two-second post-`didOpen` analysis grace period and a 30-second Windows request timeout because the pinned Windows npm wrapper can perform its first workspace analysis substantially later than the other matrix tools; later protocol failures still fail the test. The matrix records the known Windows-only gopls `no views` environment limitation as a bounded skip; Linux and macOS still require the full gopls scenarios, and all other version or protocol failures remain failures.

The native IDE will not acquire a runtime dependency on any language server. Future refinements should add server-specific fixtures for diagnostics and semantic-token edge cases without weakening the fixed-version contract.

### Real GDB and LLDB validation

The DAP matrix fixes versioned entries for GDB's DAP interpreter, LLDB-DAP, and the Windows OpenDebugAD7 contract. The runtime probe sends initialize and, for launch-capable entries, exercises launch, stopped, stack trace, scopes, variables, continuation, source/function/data breakpoint requests when advertised, and clean disconnect. It records the LLDB-DAP 18.1.3 handshake variation in which the adapter does not emit `initialized` after its initialize response. The Code::Blocks DebuggerGDB provider remains a separate, matched Linux workflow and is not conflated with generic GDB or LLDB compatibility.

The next refinement is adapter-specific fixture coverage for LLDB-DAP and OpenDebugAD7, including their source-path conventions and data-breakpoint discovery. The implementation must continue to gate each request on the adapter's initialize capabilities rather than assuming that one adapter's extensions apply to all others.

### Themes, accessibility, and visual verification

The theme model is implemented and shared by the native workbench, editor, terminal surface, diagnostics, and breakpoints. The next step is platform-specific validation at ordinary and high-DPI scales. The validation should inspect clipped labels, focus order, readable status text, gutter alignment, and contrast in the four supported theme modes.

A complete accessibility pass must also cover keyboard-only navigation, native control labels, font scaling, high-contrast system settings, focus restoration after dialogs, and non-color descriptions for diagnostics and breakpoint states. The isolated workflows now create high-contrast screenshots on all target operating systems; human review and, where available, structured UI inspection must supplement those artifacts because layout and screen-reader regressions are not reliably detected by CTest.

### Extension API contracts

The host now transports document lifecycle events and Tree View data without exposing native pointers or private Code::Blocks objects. The next API increment should version activation and failure behavior for SCM providers, custom editors, diagnostics collections, and workspace trust. Each contribution must have a deterministic smoke scenario and an explicit unsupported result when the native workbench cannot render it.

Webviews and image-rendering backends remain optional. They must not load during native startup and must be subject to explicit resource and trust policies.

### Native packaging and distribution

The installation foundation is implemented. CMake now stages relocatable resources relative to the executable, keeps user-installed extensions outside read-only installation prefixes, installs Linux desktop metadata and icon sizes, embeds the macOS icon in an application bundle, and can include configured Windows wxWidgets runtime DLLs. Isolated package workflows produce Linux Debian/tar artifacts, a macOS disk image, and a Windows portable ZIP with SHA-256 checksums.

The next distribution work is deliberately narrower than feature development. It must test each artifact in a clean environment, decide the supported dependency policy for Node.js and wxWidgets, add signing and notarization when release infrastructure is available, and create a reviewed draft release before public publication. A package artifact is not yet a stable release.

## Release 1.0 acceptance criteria

Release 1.0 requires a stable classic layout, a fast native editor, navigable Problems, structured Build sessions, a reliable PTY or ConPTY terminal, and a tested Debug workflow on all three target operating systems. A compiler, terminal, or language-server diagnostic with a reliable location must remain visible in its originating output and become a clickable source annotation.

| Area | Required evidence |
|---|---|
| Native workbench | Clean startup, compact layout, visible menus and shortcuts, collapsible secondary regions, and no Electron or Chromium dependency in the core process. |
| Editor | UTF-8 editing, undo, save, lexical fallback highlighting, semantic-token composition, navigation, completion application, hover, rename, code actions, delimiter handling, themes, and keyboard-first operation. |
| Build | Portable target discovery, user tasks and schemes, scheme-aware Build and Run, persistent sessions, conservative diagnostics, and no implicit shell execution. |
| Problems | Correct severity, source, range, stale state, filtering, gutter rendering, raw-output correlation, and navigation for compiler, terminal, LSP, and adapter diagnostics. |
| Terminal | Interactive PTY or ConPTY behavior, ANSI/VT rendering, resize, scrollback, selection, mouse reporting, bracketed paste, profiles, and safe handling of unsupported graphics payloads. |
| Debug | DAP lifecycle, automatic stopped-event refresh, active-frame editor synchronization, source mapping, watches, variables, source and advanced breakpoint requests, and explicit unsupported-capability reporting. |
| Extensions | Out-of-process command/configuration contracts, workspace document events, Tree View forwarding, trust gates, transactional installation, and compatibility reports. |
| Code::Blocks | Portable adapter-disabled build, matched-SDK adapter smoke, isolated plugin loading, value-owned events, and no foreign plugin loading in the main process. |
| Platforms | Successful clean builds and smoke tests in isolated Linux, macOS, and Windows workflows, with a separate optional Linux SDK/provider workflow. |
| Security | Workspace trust gates execution, extension and adapter boundaries remain explicit, installation is transactional, and third-party native compatibility is not overstated. |

## Explicit non-goals and post-1.0 work

The project does not promise that every VS Code extension, every Code::Blocks plugin, or every Xcode workflow will work. Electron-dependent extensions, private VS Code APIs, proprietary services, arbitrary native plugins, unsupported webviews, and unmatched Code::Blocks binaries require explicit compatibility work or remain unsupported.

Post-1.0 work may add mathematically complete Unicode grapheme and width handling, optional Sixel or Kitty rendering with strict resource limits, deeper SCM and custom-editor APIs, unattended extension registry workflows, adapter-specific data-breakpoint discovery, and broader debugger features. These capabilities will be introduced only when their native boundary, security policy, tests, and platform behavior are defined.

## References

[1]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/UI_DESIGN.md "Codium::Blocks native interface specification"
[2]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/PROBLEMS.md "Codium::Blocks diagnostics contract"
[3]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/CODEBLOCKS_INTEGRATION.md "Codium::Blocks Code::Blocks SDK integration"
[4]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/CODEBLOCKS_ADAPTER_PROTOCOL.md "Codium::Blocks adapter protocol"
[5]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/DEBUGGING.md "Codium::Blocks debugging model"
