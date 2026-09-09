# Codium::Blocks — Native, Electron-Free IDE Prototype

Codium::Blocks is a native IDE prototype for multi-language development. It combines a C++/wxWidgets application core and the Code::Blocks plugin model with an optional Node.js Extension Host that can progressively implement the VS Code extension API.

> **Goal:** keep the main application lightweight and Electron-free without giving up modern extensibility.

The target desktop platforms are **Windows, macOS, and Linux**. See [`docs/PLATFORMS.md`](docs/PLATFORMS.md) for platform-specific toolchains and data directories.

Every push to `main` and every pull request is checked by GitHub Actions on all three target operating systems. The workflow builds the native application, runs CTest, checks JavaScript syntax, and executes the Extension Host smoke test.

## Current status

The current `0.4.0` increment provides:

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

This is not full VS Code compatibility. The implementation is deliberately layered and must still add clickable diagnostic locations, richer completion/hover interaction, project workspaces, Tree Views, tasks, terminal support, and DAP.

## Architecture

```text
wxWidgets / C++
  ├── native window
  ├── editor and project model (next stages)
  ├── Code::Blocks C++ plugins (next integration)
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

1. Integrate the Code::Blocks core and preserve its C++ SDK.
2. Extend the cross-platform VSIX installer with checksums, rollback, permissions, and manifest validation.
3. Implement commands, configuration, and keybindings reflected in the wxWidgets UI.
4. Add documents, diagnostics, and LSP, starting with `clangd`, `rust-analyzer`, `gopls`, and `pyright`.
5. Add tasks, terminal support, and DAP for GDB/LLDB.
6. Implement Tree Views, SCM, themes, and optional webviews.
7. Add Open VSX and private extension registries.
8. Publish a compatibility suite using real open-source extensions.

## Security

A separate process reduces the impact of extension failures, but it is not a security sandbox. An extension may still inherit the user's permissions for files, processes, and network access. Before production distribution, the host needs workspace trust, an allowlist, logging, resource limits, package validation, and a clear permission model.

## License

The original Codium::Blocks prototype code in this repository is licensed under **GNU General Public License v3.0 only (GPL-3.0-only)**. A future integration with Code::Blocks must preserve the original project's GPL-3.0 notices and obligations. Third-party extensions and dependencies retain their own licenses.
