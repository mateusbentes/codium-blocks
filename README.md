# Codium::Blocks — Native, Electron-Free IDE Prototype

Codium::Blocks is a native IDE prototype for multi-language development. It combines a C++/wxWidgets application core and the Code::Blocks plugin model with an optional Node.js Extension Host that can progressively implement the VS Code extension API.

> **Goal:** keep the main application lightweight and Electron-free without giving up modern extensibility.

The target desktop platforms are **Windows, macOS, and Linux**. See [`docs/PLATFORMS.md`](docs/PLATFORMS.md) for platform-specific toolchains and data directories.

Every push to `main` and every pull request is checked by isolated GitHub Actions workflows for the three target operating systems. The workflows build the native application, run CTest, validate the versioned LSP/DAP matrices, check JavaScript syntax, and execute the Extension Host smoke test. The optional Code::Blocks SDK and DebuggerGDB jobs are isolated in a separate Linux workflow.

## Current status

The current `1.0.1` development increment builds on the completed `0.9.0` baseline and provides:

- a native C++/wxWidgets window;
- no Electron dependency or linkage;
- a C++ client for an out-of-process Node.js Extension Host;
- a versioned JSON Lines IPC protocol;
- CommonJS extension loading through `package.json`;
- an initial bridge for `vscode.commands`, `vscode.window`, `vscode.workspace`, `vscode.languages`, `vscode.extensions`, `vscode.Uri`, and `vscode.env`;
- extension command execution;
- offline `.vsix` installation through the cross-platform wxWidgets ZIP reader;
- a local installed-extension listing;
- an automated end-to-end smoke test;
- a demonstration extension that displays a message through the host.
- a native UTF-8 document model with open, edit, dirty-state, and save operations;
- a basic source editor surface;
- conservative native syntax highlighting for C/C++, Python, Rust, Go, Java, JavaScript, TypeScript, JSON, HTML, CSS, Markdown, YAML, and CMake;
- native editor productivity actions for Find, Replace, Replace All, Find next/previous, and Go to Line with visible keyboard shortcuts;
- persistent extension configuration stored outside the repository;
- manifest-contributed commands reflected as native UI buttons;
- an LSP process manager with standard `Content-Length` framing;
- a `clangd` launch path when `clangd` is installed on the system.
- LSP initialization and `initialized` handshake;
- `textDocument/didOpen` and `textDocument/didChange` synchronization;
- hover and completion requests;
- `textDocument/semanticTokens/full` requests with LSP legend decoding and native style composition;
- definition, declaration, references, document symbols, workspace symbols, rename, and code-action requests;
- completion items with text edits can be applied directly from the native result list;
- hover rendering preserves multiple Markdown and code-content blocks;
- Go to File, Go to Symbol, delimiter matching, automatic paired delimiters, basic indentation, and circular tab navigation;
- normalized diagnostics and language-server result events;
- an optional `tests/real-lsp-smoke.mjs` matrix that exercises installed clangd, rust-analyzer, gopls, and pyright-langserver binaries in language-appropriate temporary workspaces without making them build dependencies; the harness allows a bounded retry while gopls creates its asynchronous workspace view on slower runners, an unavailable or non-startable external toolchain is reported as skipped, and protocol failures after startup remain test failures;
- a versioned `tests/real-lsp-matrix.json` contract for clangd, rust-analyzer, gopls, and pyright, plus a versioned `tests/real-dap-matrix.json` contract for GDB, LLDB-DAP, and OpenDebugAD7;
- a mandatory integration-matrix validator and runtime probes that verify fixed tool versions, isolated fixtures, capability-gated LSP scenarios, and bounded skip reasons;
- a deterministic fake-LSP integration test.
- native File, Language, and Extensions menus;
- keyboard shortcuts for opening, saving, hover, and completion;
- a diagnostics panel fed by `textDocument/publishDiagnostics`;
- a hover/language-server result panel;
- a native completion-items list;
- language identification for C/C++, Python, Rust, Go, Java, JavaScript, TypeScript, JSON, HTML, CSS, Markdown, YAML, and CMake.
- native workspace folder opening;
- recursive file tree with generated and dependency directories filtered;
- multiple document tabs;
- a native command palette;
- workspace-relative paths and independent document buffers.
- automatic CMake, Make, Cargo, and npm toolchain detection;
- built-in Configure, Build, and Test tasks;
- an asynchronous native task runner using executable argument vectors;
- a task list, Build menu, and task output panel;
- task cancellation without requiring Electron or a POSIX shell.
- an interactive native terminal with stdin, stdout, stderr, and workspace cwd;
- a DAP client with standard `Content-Length` framing;
- debug adapter controls for initialize, launch, continue, pause, and disconnect;
- deterministic fake terminal and fake DAP transport tests.
- a PTY backend for Linux and macOS;
- a dynamically detected ConPTY backend for Windows with pipe fallback;
- terminal resize propagation;
- raw UTF-8 and ANSI sequence preservation with common color rendering;
- command history and Up/Down keyboard navigation;
- native shell selection through the terminal session API.
- a native VT screen model with cursor, cell attributes, erase, scrolling, and alternate screen;
- CSI cursor movement, SGR colors, save/restore cursor, cursor visibility, and wrap mode;
- direct keyboard forwarding for UTF-8 text, arrows, Enter, Escape, Tab, Ctrl, Alt, and editing keys;
- a deterministic terminal-screen test for Vim/curses-oriented behavior.
- a bounded visual scrollback buffer with wheel navigation;
- mouse reporting in legacy and SGR formats;
- drag selection and clipboard copy from the terminal surface;
- bracketed paste when requested by the child application;
- initial wide-Unicode and combining-character cell handling;
- persistent terminal profiles for shell, dimensions, and command history.
- OSC 8 hyperlinks with a safe `http`, `https`, `mailto`, and `file` allowlist;
- DEC synchronized updates with batched native rendering;
- expanded grapheme joining for combining marks, variation selectors, ZWJ sequences, and regional-indicator pairs;
- double-click word selection and triple-click line selection;
- safe DCS/APC graphics policy that consumes Sixel/Kitty payloads without corrupting the screen.
- native DAP breakpoint toggling and `setBreakpoints` requests;
- value-owned DAP session state with `initializing`, `initialized`, `running`, `paused`, `stopped`, and `disconnected` transitions;
- automatic DAP refresh of threads, stack frames, scopes, variables, and persistent watches after stopped events, with transient debug views cleared after continued or terminated events;
- breakpoint status tracking for pending, verified, and rejected states; a disabled state remains reserved for a future adapter capability; colored gutter markers and source-correlated adapter responses;
- native threads, call-stack, scopes, variables, and evaluate panels;
- problem-list navigation from diagnostics to editor locations;
- SHA-256 verification for VSIX artifacts;
- manifest validation before an extension is committed to the installed directory;
- transactional VSIX installation with staging and rollback of the previous version;
- HTTPS-only extension registry configuration without embedded credentials;
- workspace trust persisted outside the source tree and execution gates for tasks, terminals, adapters, hosts, and VSIX installation.
- persistent watches and DAP source-file mapping;
- adapter capability discovery and automatic `configurationDone`;
- clickable call-stack locations with source mapping;
- an Open VSX search client with HTTPS validation and offline catalog cache;
- optional Ed25519 artifact verification through OpenSSL;
- per-extension compatibility reports;
- native Tree View, Git SCM, and custom-editor registries with UI surfaces.
- workspace document open/change/save events, `workspace.textDocuments`, `TreeItem`, Tree Data Providers, and native Tree View event forwarding in the Extension Host;
- a classic native workbench layout with a project navigator, central editor, and dockable bottom workbench;
- Problems, Build, Terminal, Debug, and Output workbench pages;
- a normalized problem model for GCC/Clang, MSVC, LSP, Build, and ANSI terminal diagnostics;
- severity-aware problem summaries, stale-result state, clickable problem navigation, and inline editor underlines;
- Problems filters by severity and source, circular next/previous problem navigation, and keyboard shortcuts (`F8` and `Shift+F8`);
- a rerun action for the last Build or Configure task, available from the Build menu, Problems panel, command palette, and `Ctrl+Shift+B`;
- a native status bar showing the active document, workspace trust, language, line, and column;
- a native Code::Blocks SDK bridge that discovers headers, plugin directories, native libraries, and XML manifests without blindly loading foreign plugin code;
- a native `.cbp` importer that exposes Code::Blocks build targets in the scheme bar and task list;
- a versioned Code::Blocks host-adapter contract with normalized project, build, diagnostic, debug, and plugin-command events;
- an optional, separate Code::Blocks SDK host adapter with JSON Lines handshake, SDK/resource capability discovery, explicit matched Compiler-plugin loading, real `.cbp` loading, structured target enumeration, and real target builds;
- official Code::Blocks project, compiler, and optional Debugger lifecycle event sinks normalized across the adapter boundary, including project file changes, compiler stdout/stderr, warnings, compiler exit status, debugger launch/pause/continue/stop events, and workspace-trust checks;
- a value-owned public debugger state snapshot with explicit `debugDataUnavailable` responses for private stack, thread, breakpoint, watch, variable, and expression models that are not part of the public SDK ABI;
- an opt-in DebuggerGDB provider boundary compiled separately from an exact private source revision and ABI identity, validating the tuple at runtime and serializing frames, threads, breakpoints, watches, and expression-backed variables without exporting private pointers;
- native Debug panels that consume provider snapshots as value-owned models for call stacks, threads, breakpoints, watches, variables, source mapping, and debugger status;
- an Extensions command and command-palette action for Code::Blocks SDK discovery;
- a native editor gutter with line numbers and severity markers for active problems;
- a scheme bar for Debug/Release configuration, target, and detected toolchain selection;
- a unified target model for CMake, Make, Cargo, npm, and imported Code::Blocks projects, with target-specific Build/Run metadata;
- CMake Presets and conservative Ninja target discovery in addition to the existing generator detection;
- user-defined tasks in `.codium-blocks/tasks.tsv` and user-defined schemes in `.codium-blocks/schemes.tsv`, using direct executable argument vectors rather than implicit shell commands;
- persistent Debug/Release, target, and toolchain preferences stored per workspace;
- scheme-aware Build/Configure/Run/Debug selection, including target-specific CMake, Make, Cargo, npm, and Code::Blocks actions;
- explicit Build-and-Run orchestration that builds the selected target before running a selected run task or discovered artifact;
- generator-aware artifact discovery for CMake configurations, CMake Presets, Ninja, Cargo, Code::Blocks outputs, and user-defined scheme overrides;
- persistent Build sessions with task, target, configuration, toolchain, elapsed time, exit status, raw output, and rerun support;
- conservative CMake, Ninja, Make, linker, multiline Rust, GCC/Clang, MSVC, and ANSI diagnostic parsing in addition to existing LSP formats;
- a deterministic problem-model parser test covering compiler, Rust, Windows-path, and ANSI diagnostics.
- coordinated System, Light, Dark, and High contrast native themes with non-color gutter labels and contrast-tested palettes;
- DAP function-breakpoint and data-breakpoint request serialization, capability reporting, deterministic fake-adapter coverage, and workspace persistence for configured advanced breakpoints;
- real DAP scenarios that exercise launch, stopped events, stack traces, scopes, variables, continuation, breakpoint requests, and clean disconnect when the selected adapter and capability set support them;
- isolated Linux, macOS, and Windows CI workflows that install pinned external tools where supported and capture a high-contrast workbench at runner display resolution;
- an original blue modular-block icon family for Windows, macOS, and Linux under [`assets/icons/`](assets/icons/), with transparent, metadata-free platform assets.

This is not full VS Code or Code::Blocks compatibility yet. The implementation is deliberately layered. The versioned real-server and DAP matrices are mandatory contract checks, while runtime execution remains conditional on installed external tools and adapter capabilities. The project must still add a complete extension registry download/install workflow, deeper SCM and custom-editor contribution APIs, a complete keyboard, screen-reader, font-scaling, and high-DPI accessibility audit, and native packaging. The private DebuggerGDB provider is opt-in and requires an exact matching source, SDK, compiler, wxWidgets, architecture, language-standard, and build identity; it is not inferred from a plugin filename. The build-oriented workflow now has explicit user tasks/schemes, generator-aware discovery, artifact selection, and Build-and-Run orchestration, while more complete generator semantics and variable expansion remain future work. The editor provides Find, Replace, Go to Line, Go to File, symbol/location navigation, completion application, rich hover presentation, rename/code-action application, delimiter matching, basic indentation, and tab navigation. The debugger synchronizes the active frame with the editor, persists source and advanced breakpoint specifications, reports adapter rejection messages, and runs bounded real GDB/LLDB scenarios when the required adapter is installed. The remaining terminal goals — complete Unicode grapheme segmentation and width handling, plus optional Sixel/Kitty image rendering — remain separately scheduled. See [`docs/TASKS.md`](docs/TASKS.md), [`docs/CODEBLOCKS_INTEGRATION.md`](docs/CODEBLOCKS_INTEGRATION.md), [`docs/TERMINAL.md`](docs/TERMINAL.md), [`docs/DEBUGGING.md`](docs/DEBUGGING.md), [`docs/EXTENSIONS_SECURITY.md`](docs/EXTENSIONS_SECURITY.md), [`docs/CONTRIBUTIONS.md`](docs/CONTRIBUTIONS.md), [`docs/UI_DESIGN.md`](docs/UI_DESIGN.md), and [`docs/PROBLEMS.md`](docs/PROBLEMS.md) for the current models.

The icon family and platform placement are documented in [`docs/ICONS.md`](docs/ICONS.md).

## Architecture

```text
wxWidgets / C++
  ├── native window
  ├── editor and project model
  ├── Code::Blocks SDK bridge (safe discovery and .cbp import)
  ├── optional external Code::Blocks host adapter (separate native process)
  ├── VsixManager
  └── ExtensionHostClient
          │ JSON Lines / separate process
          ▼
      Node.js Extension Host
          └── initial vscode-compatible subset
```

Node.js is optional. The native application can start without it and launch the Extension Host only when a JavaScript or TypeScript extension is needed. Webviews are not part of the first increment.

Extension settings are stored outside the source tree: under `%APPDATA%/CodiumBlocks` on Windows, `~/Library/Application Support/CodiumBlocks` on macOS, and `$XDG_STATE_HOME/codium-blocks` or `~/.local/state/codium-blocks` on Linux. The `CODIUM_BLOCKS_DATA` environment variable can override this location for testing or portable deployments.

## Build

Ubuntu/Debian dependencies:

```bash
sudo apt-get install build-essential cmake pkg-config libwxgtk3.2-dev nodejs
```

Install `libssl-dev` as well to enable optional Ed25519 signature verification for signed extension artifacts.

The portable Codium::Blocks build does **not** require Code::Blocks or its SDK. The optional `codeblocks-dev` package is needed only when building and testing the separate real Code::Blocks host adapter. On Ubuntu/Debian, install the complete optional integration environment with:

```bash
sudo apt-get install codeblocks codeblocks-dev libtinyxml-dev xvfb
```

These packages have distinct roles: `codeblocks-dev` provides the SDK headers, shared library, and `pkg-config` metadata; `codeblocks` provides the official runtime resources and plugins; `libtinyxml-dev` provides the `tinyxml.h` header required by the SDK headers; and `xvfb` provides a virtual X display for the headless integration smoke test. They are not required by the main native executable, and they are not required on systems that only need the portable build.

To force the portable path even when the SDK is installed, disable the optional adapter explicitly:

```bash
cmake -S . -B build \
  -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=OFF \
  -DCMAKE_BUILD_TYPE=Debug
```

When the SDK is installed, the default CMake configuration can discover it through `pkg-config` and build `codium-blocks-codeblocks-adapter` separately. The regular `codium-blocks` process remains independent of `libcodeblocks` in both configurations.

Configure, build, and test:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Run the native application:

```bash
./build/codium-blocks
```

In the window, click **Start Extension Host**, then **Load demo**, and finally **Run hello.codium**.

The host can also be tested independently:

```bash
node tests/host-smoke.mjs
```

## Design principles

The main application remains native. C++ plugins are intended for deep editor, project, compiler, debugger, and operating-system integration. JavaScript and TypeScript extensions run out of process through Node.js. Electron is not required by the core application or by the first extension-host protocol.

The project does not promise that every VS Code extension will work. Compatibility is measured by API surface and tested extension scenarios. Extensions that require Electron internals, VS Code private APIs, proprietary services, or unsupported webview behavior will need a replacement or an explicit compatibility layer.

## Delivery roadmap

The verified implementation status and ordered delivery plan are maintained in [`docs/ROADMAP.md`](docs/ROADMAP.md). The debugger delivery is public: stopped events refresh the native Debug views, the selected frame is synchronized with the editor, workspace source breakpoints are restored from persistent storage, and conditional breakpoints, hit conditions, and log messages are sent through standard DAP requests. The versioned GDB/LLDB matrix and capability-gated function/data breakpoint requests are now part of the implementation; their runtime probes remain conditional on installed adapters.

The remaining 1.0 work is organized by user-visible outcomes. The ordered workstreams are broader real-server scenario coverage where adapters expose different capabilities, a complete keyboard, screen-reader, font-scaling, and high-DPI accessibility audit, deeper versioned extension APIs, and native packaging. The isolated workflows now provide fixed-tool installation steps and high-contrast visual artifacts, but those artifacts still require human review on each runner family. Packaging is intentionally deferred until the runtime contracts are stable. Each workstream is delivered as a separately tested increment with an adapter-disabled portable path and documentation in English.

The project does not claim complete VS Code, Code::Blocks, or Xcode compatibility. Compatibility is measured by the APIs, adapters, and scenarios that are implemented and tested. See [`docs/ROADMAP.md`](docs/ROADMAP.md), [`docs/UI_DESIGN.md`](docs/UI_DESIGN.md), [`docs/PROBLEMS.md`](docs/PROBLEMS.md), and [`docs/CODEBLOCKS_INTEGRATION.md`](docs/CODEBLOCKS_INTEGRATION.md) for the detailed boundaries.

## Security

A separate process reduces the impact of extension failures, but it is not a security sandbox. An extension may still inherit the user's permissions for files, processes, and network access. Before production distribution, the host needs workspace trust, an allowlist, logging, resource limits, package validation, and a clear permission model.

## License

The original Codium::Blocks prototype code in this repository is licensed under **GNU General Public License v3.0 only (GPL-3.0-only)**. A future integration with Code::Blocks must preserve the original project's GPL-3.0 notices and obligations. Third-party extensions and dependencies retain their own licenses.
