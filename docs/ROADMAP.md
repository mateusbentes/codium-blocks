# Implementation Roadmap

## Release 0.1 — proven foundation

This release proves the minimum architecture: a wxWidgets window, a separate Node.js process, a JSON Lines protocol, CommonJS extension loading, command execution, host-to-UI notifications, offline VSIX installation, and an automated smoke test.

## Release 0.2 — documents, configuration, and language-process foundation

The current `0.2.0` development increment adds a native UTF-8 document model with open/edit/save state, an editor surface, persistent Extension Host configuration, manifest-contributed commands reflected in the native UI, and an LSP process manager using standard `Content-Length` framing. `clangd` can be started through the host when installed on the system.

## Release 0.3 — declarative UI

Implement `contributes.commands`, `menus`, `keybindings`, `configuration`, `languages`, `snippets`, and `themes` as first-class native models. Validate the manifest in C++ before activation and map contributions to menus, the command palette, settings, and the editor.

## Release 0.4 — language tooling

Complete the native LSP client with initialize, shutdown, diagnostics, completion, hover, definition, formatting, and workspace/document synchronization. Validate `clangd`, `rust-analyzer`, `gopls`, Pyright, and language servers for Java, C#, PHP, and Lua.

## Release 0.5 — build and debugging

Add tasks, terminal support, problems, and DAP. The first matrix should cover CMake/Make/Ninja, Cargo, Python, and Java, together with GDB and LLDB.

## Release 0.6 — registry and security

Add Open VSX, private registries, caching, checksums, rollback, workspace trust, permissions, and per-extension compatibility reports. The registry must not install extensions that depend on APIs the host has not declared as supported.

## Release 0.7 — optional advanced UI

Add Tree Views, SCM, webviews, and custom editors. The web engine must remain optional and must not be loaded during IDE startup.

## Compatibility criteria

Every release must publish a tested matrix using real open-source extensions. Compatibility means that a package installs, activates, registers its contributions, and completes its primary scenarios. The project must not claim full compatibility while private APIs, webviews, or proprietary dependencies remain unsupported.
