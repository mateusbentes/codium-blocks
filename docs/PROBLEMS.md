# Problems, Warnings, and Errors

## Goal

Codium::Blocks 1.0 must make compiler and language feedback visible at the point where the user can act on it. A warning or error should be present in the originating Build or Terminal output, in the Problems view, and in the source editor whenever a reliable source location is available.

The system is not a second compiler. It is a normalized diagnostic layer that receives messages from language servers, compiler parsers, task runners, terminal output, and debug adapters. Raw output is always retained even when parsing fails.

## Normalized problem model

Each problem contains a source identifier, severity, message, file path or URI, zero-based start line and character, optional end position, optional diagnostic code, optional related locations, and a stale/result timestamp. The model must preserve the original message and the parser that produced it.

| Field | Required behavior |
|---|---|
| Source | Identifies clangd, GCC, Clang, MSVC, Rust, task, terminal, or another provider |
| Severity | Error, warning, information, or hint |
| Message | Keeps the complete original diagnostic text |
| Location | Resolves workspace-relative, absolute, URI, and mapped source paths |
| Range | Highlights the reported range when the provider supplies one |
| Code | Displays compiler or language-server diagnostic codes when available |
| Related information | Allows navigation to secondary files and notes |
| Freshness | Marks stale results after a document version or build changes |

## Editor presentation

The editor displays a gutter marker and an underline for active problems. Red represents errors, amber represents warnings, blue or neutral styling represents information, and a subdued marker represents hints. Color is never the only signal: every marker has an accessible label and a tooltip.

The current native implementation renders line numbers and severity markers in a dedicated editor gutter, keeps the gutter synchronized with vertical editor scrolling when the platform emits scroll events, and applies colored underlines to the active document. Problems in inactive tabs retain their file association and receive markers when that tab becomes active.

Selecting a marker opens a compact inline detail containing the complete message, source, code, and actions. The detail is an overlay or an editor-adjacent view. It must not modify the source buffer. If a problem has a quick fix or related location, those actions are shown beside the detail.

A document with several problems must display all markers without replacing one diagnostic with another. When multiple diagnostics overlap, the editor must provide a deterministic selection order and the Problems view must list every entry.

## Problems view

The Problems view is a dockable bottom-workbench panel. The current native implementation provides severity and source filters, a shown-versus-total summary, and direct navigation to the selected file and range. File/current-document filters and grouped presentation remain planned refinements. Double-clicking an entry opens its file and moves the caret to its range.

The view must support the following practical operations:

1. Navigate to the next or previous visible problem; `F8` and `Shift+F8` wrap at the ends of the filtered list.
2. Filter by error, warning, information, hint, or all severities.
3. Filter by diagnostic source or show all sources.
4. Clear a build result without deleting language-server state.
5. Re-run the last Build or Configure task after a workspace-trust check.
6. Preserve raw output access for every structured entry.

The Build menu, the Problems toolbar, and the command palette expose the same rerun action. Its stored task includes the selected scheme arguments, so rerunning a CMake Debug/Release build does not silently fall back to another configuration.

## Build and compiler parsing

The parser begins with conservative support for GCC/Clang, MSVC, Rust, ANSI-prefixed output, and common language-server formats. A line becomes a structured problem only when the parser can identify a plausible path and location. Rust diagnostics are assembled from an `error[...]` or `warning[...]` header followed by a `--> path:line:column` location. Unrecognized lines remain raw text.

Compiler output commonly appears in the following forms:

```text
src/main.cpp:42:7: error: use of undeclared identifier 'value'
main.cpp(42,7): warning C4100: unreferenced parameter
error[E0382]: borrow of moved value: `item`
```

The parser must understand workspace-relative paths, absolute paths, Windows drive paths, URI paths, spaces in file names, and source mappings. It must not confuse a colon in a Windows drive letter with a line separator.

When a build finishes, the Problems view displays the result associated with that build session. Starting a new build marks the previous build diagnostics stale until the new result replaces them. Language-server diagnostics remain independently versioned.

The build parser also recognizes CMake `Error at` locations, Ninja and Make fatal lines, linker and `collect2` failures, and ANSI-prefixed variants of these messages. A `Problem` carries the originating `buildSessionId` when it came from a Build or Code::Blocks compiler session. This allows the Build panel to show diagnostics beside the exact target, configuration, toolchain, elapsed time, exit status, and raw output that produced them, without merging unrelated invocations.

## Terminal behavior

The integrated terminal continues to display the complete ANSI transcript. Recognized error and warning locations become clickable links, but parsing must never intercept arbitrary terminal input or change the child process's bytes.

The terminal header shows the latest exit code and provides filters for errors, warnings, commands, and all output. Filters are presentation state only. The full transcript remains available for copying, search, and post-failure inspection.

## Trust and performance

Diagnostics are data, not executable actions. Opening a source location must not run a task or extension. Quick fixes and rerun actions must pass workspace-trust checks before they start a process.

Parsing must be incremental for long terminal sessions and large build logs. The UI must remain responsive while a background parser processes output. A configurable maximum number of retained structured problems prevents unbounded memory growth, while raw output may be rotated or saved separately.

## 1.0 verification scenarios

The automated and manual test matrix must cover a clean build, a failed build, a warning-only build, multiple diagnostics in one file, diagnostics in inactive tabs, an error emitted through the terminal, an LSP publish-diagnostics notification, a Windows path, a path containing spaces, and a source-mapped remote path.

## References

[1]: https://code.visualstudio.com/docs/editor/codebasics "Visual Studio Code code navigation and editor basics"
[2]: https://developer.apple.com/documentation/xcode "Apple Xcode documentation"
[3]: https://clang.llvm.org/docs/UsersManual.html "Clang user manual and diagnostics"
