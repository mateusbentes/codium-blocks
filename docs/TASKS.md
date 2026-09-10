# Project Tasks

Codium::Blocks detects common project toolchains from the workspace root and exposes them through one native target, scheme, and task model. The first task model deliberately uses an executable plus an argument vector rather than a shell command string. This avoids shell quoting differences and keeps task execution portable across Windows, macOS, and Linux.

| Manifest | Toolchain | Detected targets | Built-in tasks |
|---|---|---|---|
| `CMakeLists.txt` | CMake | Executables and libraries declared by `add_executable` and `add_library` | Configure, Build, target-specific Build |
| `Makefile` or `makefile` | Make | Top-level non-pattern targets | Build, target-specific Build |
| `Cargo.toml` | Cargo | `[[bin]]` targets, or the workspace aggregate | Build, Test, binary-specific Build |
| `package.json` | npm | Entries under `scripts` | Build, Test, script-specific Run |
| `.cbp` | Code::Blocks | Imported Code::Blocks build targets | Target-specific Code::Blocks Build or isolated adapter Build |

Each `ProjectTarget` records its stable identifier, display name, toolchain, project file, working directory, build task, expected output where it is known, and whether Run or Debug is supported. Two schemes are derived for each target: **Debug** and **Release**. The scheme bar persists the selected scheme, configuration, target, and toolchain in the workspace's `.codium-blocks/project-preferences.tsv` file. These preferences are local project state and do not alter the source project manifest.

The Build command first selects a target-specific task when one exists. CMake receives the selected build directory and target, Make receives the selected goal, Cargo receives the selected binary, npm runs the selected script, and Code::Blocks uses the imported `.cbp` target or the isolated adapter when that adapter is ready. **Run target** and DAP launch prefer the executable or script associated with the active scheme; an explicit file chooser remains available when artifact discovery is incomplete.

## Build sessions

Every Build or Configure action creates a persistent `BuildSession`. A session records the task name, target, configuration, toolchain, project file, working directory, start time, elapsed time, exit status, cancellation state, and raw output. The latest twenty sessions are stored at `.codium-blocks/build-sessions.tsv` inside the workspace. The Build panel presents the session history and can restore a previous session's output. Problems parsed from task and compiler output retain the originating session identifier, allowing later UI work to correlate diagnostics with the exact Build invocation.

The session store uses an escaped tab-separated format and atomic replacement on save. It is intentionally not a shell history and does not execute commands when loaded. A running session is marked cancelled if a newer Build starts or the user stops the task. A failed process start produces a failed session rather than leaving an indefinitely running record.

## Diagnostics

Task standard output and standard error are collected asynchronously by the native `TaskRunner` and displayed in the Build panel. The normalized Problems model accepts GCC/Clang, MSVC, CMake, Rust, Ninja, Make, linker, and ANSI-prefixed diagnostics. Lines that do not contain a reliable source location remain visible as raw Build output and are promoted to a problem only when their tool explicitly reports an error condition. The UI remains responsive while a task is running, and a task can be stopped from the Build menu, command palette, or workspace controls.

The initial implementation does not execute a shell. A future explicit task configuration format can add user-defined tasks and schemes while preserving the same structured process contract. Terminal emulation is a separate feature and remains opt-in; task execution itself does not require Electron, a webview, or a POSIX-only command interpreter.
