# Codium::Blocks — Native, Electron-Free IDE Prototype

Codium::Blocks is a native IDE prototype for multi-language development. It combines a C++/wxWidgets application core and the Code::Blocks plugin model with an optional Node.js Extension Host that can progressively implement the VS Code extension API.

> **Goal:** keep the main application lightweight and Electron-free without giving up modern extensibility.

The target desktop platforms are **Windows, macOS, and Linux**. See [`docs/PLATFORMS.md`](docs/PLATFORMS.md) for platform-specific toolchains and data directories.

Every push to `main` and every pull request is checked by GitHub Actions on all three target operating systems. The workflow builds the native application, runs CTest, checks JavaScript syntax, and executes the Extension Host smoke test.

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
- persistent extension configuration stored outside the repository;
- manifest-contributed commands reflected as native UI buttons;
- an LSP process manager with standard `Content-Length` framing;
- a `clangd` launch path when `clangd` is installed on the system.
- LSP initialization and `initialized` handshake;
- `textDocument/didOpen` and `textDocument/didChange` synchronization;
- hover and completion requests;
- normalized diagnostics and language-server result events;
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
- a Phase E.1 value-owned debugger state snapshot with explicit `debugDataUnavailable` responses for private stack, thread, breakpoint, watch, variable, and expression models that are not part of the public SDK ABI;
- an opt-in Phase E.2 DebuggerGDB provider boundary that is compiled separately from an exact private source revision and ABI identity, validates the tuple at runtime, and serializes frames, threads, breakpoints, and watches without exporting private pointers;
- an Extensions command and command-palette action for Code::Blocks SDK discovery;
- a native editor gutter with line numbers and severity markers for active problems;
- a scheme bar for Debug/Release configuration, target, and detected toolchain selection;
- scheme-aware CMake Build/Configure arguments and Build-panel focus;
- conservative multiline Rust diagnostics in addition to GCC/Clang, MSVC, and ANSI formats;
- a deterministic problem-model parser test covering compiler, Rust, Windows-path, and ANSI diagnostics.
- an original blue modular-block icon family for Windows, macOS, and Linux under [`assets/icons/`](assets/icons/), with transparent, metadata-free platform assets.

This is not full VS Code or Code::Blocks compatibility yet. The implementation is deliberately layered and must still add richer completion/hover interaction, explicit user task configuration, a complete extension registry download/install workflow, deeper Tree View/SCM contribution APIs, and broader debugger source-location and UI integration. The private DebuggerGDB provider is opt-in and requires an exact matching source, SDK, compiler, wxWidgets, architecture, language-standard, and build identity; it is not built by portable CI or inferred from a plugin filename. The remaining 1.0 interface work includes richer source annotations, keyboard navigation, themes, and cross-platform visual verification. The remaining terminal goals — complete Unicode grapheme segmentation and width handling, plus optional Sixel/Kitty image rendering — remain separately scheduled. See [`docs/TASKS.md`](docs/TASKS.md), [`docs/CODEBLOCKS_INTEGRATION.md`](docs/CODEBLOCKS_INTEGRATION.md), [`docs/TERMINAL.md`](docs/TERMINAL.md), [`docs/DEBUGGING.md`](docs/DEBUGGING.md), [`docs/EXTENSIONS_SECURITY.md`](docs/EXTENSIONS_SECURITY.md), [`docs/CONTRIBUTIONS.md`](docs/CONTRIBUTIONS.md), [`docs/UI_DESIGN.md`](docs/UI_DESIGN.md), and [`docs/PROBLEMS.md`](docs/PROBLEMS.md) for the current models.

The icon family and platform placement are documented in [`docs/ICONS.md`](docs/ICONS.md).

## Architecture

```text
wxWidgets / C++
  ├── native window
  ├── editor and project model (next stages)
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

## Implementation roadmap

1. Complete the staged Code::Blocks host boundary: the optional adapter now provides matched-SDK bootstrap, real `.cbp` target enumeration, official project/compiler/Debugger events, real Compiler-plugin builds with output capture, explicit debugger launch/control, a safe public-state snapshot boundary, and an opt-in exact DebuggerGDB provider for value-owned frames, threads, breakpoints, and watches. Next comes broader debugger data coverage and a reviewed native-plugin policy.
2. Complete the cross-platform VSIX and Open VSX workflow with checksums, signatures, rollback, permissions, caching, and manifest validation.
3. Deliver the classic native workbench described in [`docs/UI_DESIGN.md`](docs/UI_DESIGN.md): project navigator, scheme bar, tabbed editor, dockable bottom panels, compact toolbar, light/dark themes, and keyboard-first navigation.
4. Deliver the normalized diagnostics contract described in [`docs/PROBLEMS.md`](docs/PROBLEMS.md), including inline warnings and errors, gutter markers, clickable terminal/build locations, stale-result handling, and Problems navigation.
5. Complete documents, diagnostics, and LSP for `clangd`, `rust-analyzer`, `gopls`, and `pyright` scenarios.
6. Complete tasks, terminal support, and DAP for GDB/LLDB workflows with structured Build output and scheme-based Run/Debug actions.
7. Deepen Tree Views, SCM, custom editors, themes, and optional webviews without loading browser backends during startup.
8. Complete Unicode grapheme/width handling and optional Sixel/Kitty rendering with resource limits.
9. Publish a compatibility suite using real open-source extensions on Windows, macOS, and Linux.

## Security

A separate process reduces the impact of extension failures, but it is not a security sandbox. An extension may still inherit the user's permissions for files, processes, and network access. Before production distribution, the host needs workspace trust, an allowlist, logging, resource limits, package validation, and a clear permission model.

## License

The original Codium::Blocks prototype code in this repository is licensed under **GNU General Public License v3.0 only (GPL-3.0-only)**. A future integration with Code::Blocks must preserve the original project's GPL-3.0 notices and obligations. Third-party extensions and dependencies retain their own licenses.
