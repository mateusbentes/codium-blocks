# Native Contributions

Codium::Blocks provides native contribution registries that do not require Electron. `TreeViewRegistry` registers a view identifier, a title, and a list of labels. The workspace view is populated from the native workspace model and is shown beside the file tree. Extension-host Tree Data Providers are forwarded as value-owned labels and metadata to the same native surface.

The out-of-process Node.js Extension Host supports `window.registerTreeDataProvider`, `window.createTreeView`, `TreeItem`, and `TreeItemCollapsibleState`. An extension can return strings or `TreeItem` values from `getChildren`. The host serializes labels, identifiers, tooltips, collapse state, and command metadata into a `treeView` event. The native process consumes this event without receiving JavaScript objects or native pointers.

Workspace document lifecycle events are also versioned at the process boundary. The native client sends `open`, `change`, and `save` events with a URI, language identifier, version, and text snapshot. The host updates `workspace.textDocuments` and invokes `workspace.onDidOpenTextDocument`, `workspace.onDidChangeTextDocument`, and `workspace.onDidSaveTextDocument` listeners. Listener failures are reported as extension events instead of terminating the host.

`ScmModel` detects a Git worktree and invokes `git status --porcelain` with an explicit working directory. The result is shown in the Source Control panel. A clean repository is reported explicitly, while non-Git workspaces remain usable and show a diagnostic instead of failing startup.

`CustomEditorRegistry` maps file extensions to editor identifiers. The current native surface still uses the wxWidgets text editor for registered files, but records the selected custom-editor identifier and exposes the registry in the UI. This is the compatibility seam for future binary, notebook, image, and domain-specific editors.

These registries remain smaller than the VS Code contribution API. They provide stable native foundations while JavaScript contribution points are progressively added to the Extension Host. SCM provider methods, custom-editor activation, webviews, arbitrary renderer APIs, and complete VS Code compatibility remain outside this increment. The deterministic native-contributions test and Extension Host smoke test cover the currently supported boundaries.
