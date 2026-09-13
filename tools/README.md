# Maintained quality tools

The scripts under `scripts/` and the pinned Python requirement under `tools/coverage/` are project-owned build tooling. They are intentionally small, deterministic, and run without network access after the workflow installs their explicitly pinned tool packages. Generated SBOMs describe the tree or staged product that was actually scanned; they do not replace a human review of wxWidgets, OpenSSL, Node.js, Code::Blocks, or platform runtime notices.
