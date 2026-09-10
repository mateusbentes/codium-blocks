# Project Tasks

Codium::Blocks detects common project toolchains from the workspace root and exposes them through one native target, scheme, and task model. The process contract is always an executable plus an argument vector. A task is never converted into a shell command string, so quoting, redirection, globbing, and shell startup remain outside the task runner's implicit behavior on Windows, macOS, and Linux.

| Manifest or build file | Toolchain | Detected targets | Built-in tasks |
|---|---|---|---|
| `CMakeLists.txt` | CMake | Executables and libraries declared by `add_executable` and `add_library` | Configure, Build, target-specific Build |
| `CMakePresets.json` or `CMakeUserPresets.json` | CMake Presets | Targets use the first discovered preset binary directory when available | Preset-based Configure, Build, target-specific Build |
| `build.ninja` in `build`, `out`, or the workspace root | Ninja | Conservative `build` rules, with executable linker rules marked runnable | Build, target-specific Build |
| `Makefile` or `makefile` | Make | Top-level non-pattern targets | Build, target-specific Build |
| `Cargo.toml` | Cargo | `[[bin]]` targets, or the workspace aggregate | Build, Test, binary-specific Build |
| `package.json` | npm | Entries under `scripts` | Build, Test, script-specific Run |
| `.cbp` | Code::Blocks | Imported Code::Blocks build targets | Target-specific Code::Blocks Build or isolated adapter Build |

Each `ProjectTarget` records its stable identifier, display name, toolchain, project file, working directory, build task, build directory when known, artifact candidates, and whether Run or Debug is supported. Two built-in schemes are derived for each target: **Debug** and **Release**. The scheme bar persists the selected scheme, configuration, target, and toolchain in `.codium-blocks/project-preferences.tsv`. These preferences are local project state and do not alter the source project manifest.

## User-defined tasks

User tasks are loaded from `.codium-blocks/tasks.tsv` when that file exists. The file is an escaped tab-separated format with one task per line and the following columns:

| Column | Meaning |
|---|---|
| `name` | Display name shown in the task list |
| `program` | Executable name or absolute executable path |
| `arguments` | Arguments separated by the literal unit-separator character `U+001F` |
| `workingDirectory` | Process working directory |
| `projectFile` | Optional project file associated with the task |
| `targetName` | Optional target associated with the task |
| `kind` | `generic`, `configure`, `build`, `run`, or `test` |
| `toolchain` | Optional scheme/toolchain label; defaults to `Custom` |

A task's `program` and each argument are passed directly to the native process API. The format does not interpret shell syntax. Users who need a pipeline, redirection, or a shell-specific feature must declare the shell executable explicitly as the task program and pass its script arguments explicitly.

For example, the following task runs CMake without invoking a shell:

```text
CMake configure Debug\tcmake\t-S <U+001F> /path/to/workspace <U+001F> -B <U+001F> /path/to/workspace/build\t/path/to/workspace\t\t\tconfigure\tCMake
```

In the physical file, each `<U+001F>` marker above is the literal unit-separator character between adjacent argument values; spaces around the marker are shown only to keep the example readable.

The current loader preserves escaped tabs, newlines, and backslashes. Workspace variable expansion in user task arguments is intentionally limited; paths should be written explicitly until a versioned variable contract is added.

## User-defined schemes

User schemes are loaded from `.codium-blocks/schemes.tsv`. The columns are `name`, `configuration`, `target`, `toolchain`, `projectFile`, `buildTaskName`, `runTaskName`, and `artifactPath`. The first four columns are required. `buildTaskName` selects the task to execute before Build and Run. `runTaskName` selects an explicit `run` task after a successful build. `artifactPath` overrides the automatic artifact candidates and may be relative to the selected target's working directory.

A scheme can therefore bind a custom task to a detected target without modifying `CMakeLists.txt`, `Makefile`, `Cargo.toml`, `package.json`, or a `.cbp` file. Scheme and task files remain workspace-local and are covered by workspace trust before execution.

## Build, Run, and Build-and-Run

The Build command first selects a target-specific task when one exists. CMake receives the selected build directory, configuration, and target. CMake Presets use their declared configure preset. Ninja receives `-C <build-directory>` and the selected target. Make receives the selected goal. Cargo receives the selected binary. npm runs the selected script. Code::Blocks uses the imported `.cbp` target or the isolated adapter when that adapter is ready.

**Build and Run** is explicit. When the selected target is runnable, Codium::Blocks executes the scheme's `buildTaskName` or the target's detected build task first. It runs the target only after a successful build. A custom `runTaskName` takes precedence over direct artifact execution. Otherwise, the native artifact discovery order includes the scheme override, generator-specific candidates, the target output path, and the target run path. Debug and Release candidates are considered separately, and Windows executable suffixes are handled without changing the stored target name.

A Run action does not create a Build session. The preceding Build or Configure action does create a persistent session, so the Build panel and Problems panel retain the exact compilation context. If the build succeeds but no artifact exists at a reliable candidate path, the IDE reports that limitation and does not guess by scanning unrelated files.

## Build sessions

Every Build or Configure action creates a persistent `BuildSession`. A session records the task name, target, configuration, toolchain, project file, working directory, start time, elapsed time, exit status, cancellation state, and raw output. The latest twenty sessions are stored at `.codium-blocks/build-sessions.tsv` inside the workspace. The Build panel presents the session history and can restore a previous session's output. Problems parsed from task and compiler output retain the originating session identifier, allowing later UI work to correlate diagnostics with the exact Build invocation.

The session store uses an escaped tab-separated format and atomic replacement on save. It is intentionally not a shell history and does not execute commands when loaded. A running session is marked cancelled if the user stops the task. A failed process start produces a failed session rather than leaving an indefinitely running record.

## Diagnostics

Task standard output and standard error are collected asynchronously by the native `TaskRunner` and displayed in the Build panel. The normalized Problems model accepts GCC/Clang, MSVC, CMake, Rust, Ninja, Make, linker, and ANSI-prefixed diagnostics. Lines that do not contain a reliable source location remain visible as raw Build output and are promoted to a problem only when their tool explicitly reports an error condition. The UI remains responsive while a task is running, and a task can be stopped from the Build menu, command palette, or workspace controls.

The configuration files are deliberately small and versionable. They do not claim compatibility with VS Code's task or launch schemas, and they do not grant a task additional permissions beyond the user's workspace-trust decision.
