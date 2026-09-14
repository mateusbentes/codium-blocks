#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Codium::Blocks Contributors

"""Audit declared package dependencies and immutable GitHub Action references."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ACTION_RE = re.compile(r"uses:\s*([^\s#]+)")
SHA_RE = re.compile(r"^[0-9a-fA-F]{40}$")
DEPENDENCY_KEYS = ("dependencies", "devDependencies", "optionalDependencies", "peerDependencies")
IGNORED_PARTS = {
    ".git", ".quality-venv", ".venv", "__pycache__", "artifacts", "build", "build-fuzz",
    "build-libfuzzer", "build-sanitized", "coverage", "node_modules",
}
IGNORED_PREFIXES = ("build-", "coverage-")


def ignored(path: Path, root: Path) -> bool:
    return any(part in IGNORED_PARTS or part.startswith(IGNORED_PREFIXES)
               for part in path.relative_to(root).parts)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    findings = []
    package_files = sorted(path for path in root.rglob("package.json") if not ignored(path, root))
    for path in package_files:
        manifest = json.loads(path.read_text(encoding="utf-8"))
        dependencies = {
            key: sorted((manifest.get(key) or {}).keys())
            for key in DEPENDENCY_KEYS
            if manifest.get(key)
        }
        if dependencies:
            lock_candidates = [path.with_name(name) for name in (
                "package-lock.json", "npm-shrinkwrap.json", "yarn.lock", "pnpm-lock.yaml"
            )]
            if not any(candidate.is_file() for candidate in lock_candidates):
                findings.append({"kind": "missing-lock", "path": str(path.relative_to(root)), "dependencies": dependencies})
            else:
                findings.append({"kind": "declared-node-dependencies", "path": str(path.relative_to(root)), "dependencies": dependencies})
    action_files = sorted((root / ".github" / "workflows").glob("*.yml")) + sorted((root / ".github" / "workflows").glob("*.yaml"))
    for path in action_files:
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            match = ACTION_RE.search(line)
            if not match:
                continue
            ref = match.group(1).split("@", 1)[1] if "@" in match.group(1) else ""
            if not SHA_RE.fullmatch(ref):
                findings.append({"kind": "mutable-action-ref", "path": str(path.relative_to(root)), "line": line_number, "value": match.group(1)})
    report = {
        "schemaVersion": 1,
        "policy": "Node dependencies require a committed lockfile; GitHub Actions must use full commit SHA references.",
        "packageManifests": [str(path.relative_to(root)) for path in package_files],
        "findings": findings,
        "status": "fail" if args.strict and findings else "pass"
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 1 if report["status"] == "fail" else 0


if __name__ == "__main__":
    raise SystemExit(main())
