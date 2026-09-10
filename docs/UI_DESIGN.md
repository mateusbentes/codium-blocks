# Codium::Blocks 1.0 — Native IDE Interface Design

## Product direction

Codium::Blocks 1.0 will preserve the compact, direct character of the classic Code::Blocks interface while adding the practical feedback and navigation expected from a modern native IDE. The result should feel familiar to C and C++ developers, remain useful for other languages, and avoid the visual and resource cost of an Electron workbench.

The design is inspired by the clarity of Code::Blocks and by the workflow conveniences commonly associated with Xcode, such as a strong issue navigator, inline diagnostics, build schemes, structured build logs, and direct navigation from a problem to its source location. It is not intended to reproduce Xcode, Code::Blocks, or any proprietary visual identity.

The primary design rule is **information density without visual noise**. The user should be able to see the project, the active source file, the current build configuration, and the most important warnings or errors without opening several unrelated dialogs.

## Layout model

| Region | Purpose | 1.0 behavior |
|---|---|---|
| Menu and toolbar | File, Edit, View, Navigate, Build, Debug, Terminal, Extensions, and Help actions | Native menus, visible keyboard shortcuts, compact toolbar, and configurable command groups |
| Scheme bar | Active workspace, target, configuration, architecture, and run/debug action | One compact selector for Debug/Release, target, and toolchain; no modal configuration dialog for routine builds |
| Project navigator | Workspace tree, filters, targets, open files, and optional Tree Views | Fast file discovery, generated-file filtering, target grouping, and persistent selection |
| Editor area | Tabbed source editing | Multiple tabs, split views, breadcrumbs, line numbers, current-line highlight, code folding, and diagnostic markers |
| Inspector and context area | File, symbol, target, and debug context | Optional right-side pane that can be hidden to preserve the classic compact layout |
| Bottom workbench | Problems, Build, Debug, Terminal, Output, and Tasks | One dockable notebook with counts, severity filters, and remembered panel state |
| Status bar | Language, encoding, line/column, indentation, build state, and workspace trust | Compact persistent status with actionable links rather than decorative indicators |

The default layout should be comfortable at 1280×800 and remain usable at smaller sizes. Every secondary region must be collapsible. The application must not require a permanent right-side panel or a large welcome surface.

## Visual language

The default theme should use the restrained, high-contrast appearance associated with traditional native IDEs: a neutral window background, clear separators, compact toolbars, readable labels, and restrained accent colors. A dark theme must be provided, but the dark theme must preserve the same information hierarchy rather than becoming a separate visual product.

The interface should use native wxWidgets controls where they provide correct keyboard, accessibility, focus, and platform behavior. Custom drawing should be limited to the editor annotations, terminal screen, severity markers, and other areas where native controls cannot provide the required interaction.

Color must not be the only indicator of state. Errors, warnings, notes, breakpoints, and trust status must also have icons, text labels, or tooltips. The UI should support high-contrast system settings and scale cleanly with platform font scaling.

## Editor feedback and diagnostics

Warnings and errors must appear directly in the source editor, not only in a separate panel. The diagnostics pipeline merges messages from language servers, compiler output, task output, and debug adapters into one normalized problem model.

Each problem has a source, severity, message, file, line, column, optional range, optional code, and optional related information. The editor renders a severity-specific underline, a gutter marker, and a hover tooltip. Selecting a marker opens the complete message and exposes navigation to related locations. The Problems panel remains the authoritative list and currently provides filtering by source and severity; file and active-document filters remain planned refinements.

The editor must distinguish the following states:

| State | Editor presentation | Problems presentation |
|---|---|---|
| Error | Red gutter marker and red wavy underline | Error count, source, code, and direct navigation |
| Warning | Amber gutter marker and amber underline | Warning count and filterable warning entry |
| Information | Blue or neutral information marker | Informational entry without aggressive visual emphasis |
| Hint | Subtle hint marker | Hidden by default when the user selects an errors-and-warnings view |
| Stale result | Faded marker or stale badge | Source and timestamp shown so the user can distinguish old output |
| Suppressed result | No active underline | Available through an explicit suppressed or filtered view |

Diagnostics from compiler output must remain visible in the terminal or Build panel as raw text. The same message must also become a structured problem when the parser can identify a path and location. A click on a compiler message, a terminal message, or a Problems entry must open the file and move the caret to the reported location.

The editor must provide a lightweight inline summary at the end of a line when the diagnostic is selected. This summary must not permanently replace source text or alter the document buffer. A tooltip or expandable inline detail must show the full message, code, source, and available quick actions.

## Build and run workflow

The scheme bar must expose the active configuration, target, and toolchain. The first-class actions are **Build**, **Run**, **Test**, **Clean**, **Build and Run**, and **Debug**. These actions use the existing task model and must work with CMake, Make, Cargo, npm-based projects, and explicit user tasks.

Build output must be retained as a structured session. The Build panel provides raw output, a problem summary, elapsed time, exit status, and a rerun action. The current implementation preserves the last Build or Configure task with its selected scheme arguments and exposes rerun from the Build menu, Problems panel, command palette, and `Ctrl+Shift+B`. The terminal remains available for interactive commands, while the Build panel is optimized for repeatable tasks and navigation.

The terminal and Build panel must recognize common compiler formats, including GCC/Clang, MSVC, Rust, and language-server diagnostics. Parsing must be conservative: when a line cannot be identified reliably, it remains raw output and is never presented as a false error.

## Terminal integration

The integrated terminal remains a real PTY or ConPTY session. It must retain ANSI colors, cursor behavior, scrollback, selection, bracketed paste, hyperlinks, and interactive keyboard input. Terminal output that contains a recognized `path:line:column` location must become clickable without disabling normal terminal behavior.

The terminal panel should provide filters for errors, warnings, command output, and system output. Filtering affects presentation only and must never delete the underlying session transcript. A failed command must show its exit status in the panel header and in the status bar until the next command succeeds or the user dismisses the state.

## Debugging workflow

The Debug panel should use the same visual language as Problems and Build. Breakpoints appear in the editor gutter and in a compact breakpoint list. The call stack is selectable, and selecting a frame opens the mapped source file at the reported line and column. Variables, watches, threads, scopes, and the debug console remain dockable in the bottom workbench.

The active debug state must be visible in the toolbar and status bar. Unsupported adapter capabilities must be shown as disabled actions with an explanation instead of causing a silent failure.

## Navigation and keyboard workflow

The interface must be keyboard-first without requiring the user to memorize hidden commands. Common actions must have visible shortcuts in menus and tooltips. The first 1.0 navigation set is:

| Action | Expected behavior |
|---|---|
| Go to file | Search workspace-relative files and open the selected result |
| Go to symbol | Search symbols supplied by the language server or project index |
| Go to line | Move to a line and column in the active document |
| Find and replace | Search, replace one occurrence, or replace all occurrences in the active document (`Ctrl+F`, `Ctrl+H`) |
| Find next/previous | Move through matches and wrap at either end of the active document (`F3`, `Shift+F3`) |
| Next problem | Move to the next visible filtered problem across the workspace (`F8`, wrapping at the end) |
| Previous problem | Move to the previous visible filtered problem (`Shift+F8`, wrapping at the beginning) |
| Toggle problem panel | Focus or collapse the Problems view |
| Command palette | Search all visible commands and show their shortcuts |
| Build and run | Build the active scheme and launch it when the build succeeds |
| Focus terminal | Move focus to the active terminal without opening a new process |

Mouse interaction must remain direct. Double-clicking a problem opens its source location. Clicking a file path in Build or Terminal output opens the file. Clicking a warning or error marker opens the detail without changing the document text.

The first Phase G editor increments implement the Find, Find next, Find previous, Replace, Replace all, and Go to Line actions through native wxWidgets controls, together with conservative lexical syntax highlighting for common constructs in the detected language. When a language server advertises semantic tokens, the client reads its legend, requests full-document tokens, decodes delta positions, and overlays the result on the lexical styles. HTML `script` and `style` blocks receive bounded JavaScript and CSS fallback tokenization. The search model also has case-sensitive and case-insensitive operations for future search-option controls. Highlighting remains intentionally layered rather than a universal parser; language-server semantic analysis, embedded grammars beyond the supported HTML regions, go-to-file, go-to-symbol, and richer replacement options remain future work.

## Extension and native contribution boundaries

Native C++ modules remain responsible for the editor, project model, compiler integration, terminal, debugger, SCM, and operating-system integration. The Node.js Extension Host remains responsible for JavaScript and TypeScript extensions. Extensions may contribute commands, configuration, languages, Tree Views, SCM providers, and custom-editor descriptors through versioned contracts.

Webviews and image-rendering backends remain optional. They must not be loaded during startup and must be subject to explicit resource and trust policies. A contribution that cannot be rendered natively must be reported as unsupported rather than silently replacing the entire native workbench with a browser surface.

## 1.0 acceptance criteria

Release 1.0 is ready only when the default interface provides a stable classic layout, a fast editor, navigable Problems, structured Build output, a usable PTY/ConPTY terminal, and a tested Debug workflow. A compiler warning or error must be visible both in its originating output and as a clickable source annotation whenever a reliable location is available.

The 1.0 test matrix must include clean builds, failed builds, warnings, multiple diagnostics on one line, diagnostics in files outside the active tab, terminal-generated diagnostics, language-server diagnostics, Unicode source, high-contrast rendering, keyboard-only navigation, workspace trust, and all three target platforms.

The project must not claim that every VS Code extension or every Xcode-like workflow is supported. Compatibility must be described by tested scenarios and by the documented native contribution contracts.

## References

[1]: https://developer.apple.com/design/human-interface-guidelines/ "Apple Human Interface Guidelines"
[2]: https://code.visualstudio.com/docs/editor/editingevolved "Visual Studio Code editing and navigation documentation"
[3]: https://www.codeblocks.org/ "Code::Blocks project website"
