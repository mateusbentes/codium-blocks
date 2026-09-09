# Project Tasks

Codium::Blocks detects common project toolchains from the workspace root and exposes them through the native task list, the Build menu, and the command palette. The first task model deliberately uses executable plus argument vectors rather than shell command strings. This avoids shell quoting differences and keeps the task runner portable across Windows, macOS, and Linux.

| Manifest | Toolchain | Built-in tasks |
|---|---|---|
| `CMakeLists.txt` | CMake | Configure, Build |
| `Makefile` or `makefile` | Make | Build |
| `Cargo.toml` | Cargo | Build, Test |
| `package.json` | npm | Build, Test |

Each task has a name, executable, argument list, and working directory. Task output and standard error are collected asynchronously by the native `TaskRunner` and displayed in the Output / task log panel. The UI remains responsive while a task is running, and a task can be stopped from the Build menu, command palette, or workspace controls.

The initial implementation does not execute a shell. A future explicit task configuration format can add user-defined tasks while preserving the same structured process contract. Terminal emulation is a separate feature and must remain opt-in; task execution itself does not require Electron, a webview, or a POSIX-only command interpreter.
