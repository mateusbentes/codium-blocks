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

## Release 0.6 — build and debugging

Add tasks, terminal support, problems, and DAP. The first matrix should cover CMake/Make/Ninja, Cargo, Python, and Java, together with GDB and LLDB.

## Release 0.7 — registry and security

Add Open VSX, private registries, caching, checksums, rollback, workspace trust, permissions, and per-extension compatibility reports. The registry must not install extensions that depend on APIs the host has not declared as supported.

## Release 0.8 — optional advanced UI

Add Tree Views, SCM, webviews, and custom editors. The web engine must remain optional and must not be loaded during IDE startup.

## Compatibility criteria

Every release must publish a tested matrix using real open-source extensions. Compatibility means that a package installs, activates, registers its contributions, and completes its primary scenarios. The project must not claim full compatibility while private APIs, webviews, or proprietary dependencies remain unsupported.
