#!/usr/bin/env python3
"""Audit GitHub workflow contracts that are easy to regress locally."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

REQUIRED_PUSH_WORKFLOWS = {
    "ci-codeblocks-linux.yml",
    "ci-linux.yml",
    "ci-macos.yml",
    "ci-windows.yml",
    "coverage-linux.yml",
    "fuzz-linux.yml",
    "real-toolchains-linux.yml",
    "real-toolchains-macos.yml",
    "real-toolchains-windows.yml",
    "fuzz-guided-linux.yml",
    "package-linux.yml",
    "package-macos.yml",
    "package-windows.yml",
}
PLATFORM_RULES = {
    "real-toolchains-linux.yml": ("ubuntu", ("macos", "darwin", "windows", "win32")),
    "real-toolchains-macos.yml": ("macos", ("ubuntu", "linux", "windows", "win32")),
    "real-toolchains-windows.yml": ("windows", ("ubuntu", "linux", "macos", "darwin")),
}
SHA_REF = re.compile(r"uses:\s*[^\s@]+@([0-9a-fA-F]{40})(?:\s|$)")


def has_push(text: str) -> bool:
    return bool(re.search(r"(?m)^\s{2}push:\s*$", text))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    workflow_dir = args.root / ".github" / "workflows"
    findings: list[str] = []
    checked: list[str] = []

    for name in sorted(REQUIRED_PUSH_WORKFLOWS):
        path = workflow_dir / name
        if not path.is_file():
            findings.append(f"missing required workflow: {name}")
            continue
        text = path.read_text(encoding="utf-8")
        checked.append(name)
        if not has_push(text):
            findings.append(f"{name}: missing unrestricted push trigger")

    for path in sorted(workflow_dir.glob("*.yml")):
        text = path.read_text(encoding="utf-8")
        for line_number, line in enumerate(text.splitlines(), start=1):
            if "uses:" not in line or "docker://" in line:
                continue
            if not SHA_REF.search(line):
                findings.append(f"{path.relative_to(args.root)}:{line_number}: action is not pinned to a full commit SHA")

    for name, (required, forbidden) in PLATFORM_RULES.items():
        path = workflow_dir / name
        if not path.is_file():
            continue
        lower = path.read_text(encoding="utf-8").lower()
        for token in forbidden:
            if token in lower:
                findings.append(f"{name}: platform-isolation token is forbidden: {token}")
        if required not in lower:
            findings.append(f"{name}: expected platform token is absent: {required}")

    result = {
        "schemaVersion": 1,
        "status": "pass" if not findings else "fail",
        "checkedPushWorkflows": checked,
        "findings": findings,
    }
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if not findings else 1


if __name__ == "__main__":
    raise SystemExit(main())
