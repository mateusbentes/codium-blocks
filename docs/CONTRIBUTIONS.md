# Native Contributions

Codium::Blocks 0.9.0 provides native contribution registries that do not require Electron. `TreeViewRegistry` registers a view identifier, a title, and a list of labels. The workspace view is populated from the native workspace model and is shown beside the file tree.

`ScmModel` detects a Git worktree and invokes `git status --porcelain` with an explicit working directory. The result is shown in the Source Control panel. A clean repository is reported explicitly, while non-Git workspaces remain usable and show a diagnostic instead of failing startup.

`CustomEditorRegistry` maps file extensions to editor identifiers. The current native surface still uses the wxWidgets text editor for registered files, but records the selected custom-editor identifier and exposes the registry in the UI. This is the compatibility seam for future binary, notebook, image, and domain-specific editors.

These registries are intentionally smaller than the VS Code contribution API. They provide a stable native foundation while JavaScript contribution points are progressively added to the Extension Host. The deterministic native-contributions test covers Tree View registration, Git status discovery, and custom-editor resolution.
