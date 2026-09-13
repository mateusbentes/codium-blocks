#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Codium::Blocks Contributors

"""Bounded mutation smoke for the native test suite.

This is intentionally small and deterministic. It creates disposable copies,
changes one exact expression, builds only the affected target, and requires the
corresponding existing test to fail. A survivor is a hard failure.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Mutation:
    name: str
    source: str
    needle: str
    replacement: str
    target: str
    test: str


MUTATIONS = (
    Mutation(
        "terminal-carriage-return",
        "native/src/terminal_screen.cpp",
        "void TerminalScreen::CarriageReturn()\n{\n    cursorColumn_ = 0;",
        "void TerminalScreen::CarriageReturn()\n{\n    cursorColumn_ = 1;",
        "codium-blocks-terminal-screen-smoke",
        "^terminal-screen-smoke$",
    ),
    Mutation(
        "registry-url-allowlist",
        "native/src/extension_security.cpp",
        'return url.StartsWith(wxS("https://")) && !url.Contains(wxS("@")) && !url.Contains(wxS("\\\\"));',
        "return true;",
        "codium-blocks-security-smoke",
        "^security-smoke$",
    ),
    Mutation(
        "dap-content-length-marker",
        "native/src/dap_client.cpp",
        "if (markerStart == std::string::npos)",
        "if (markerStart != std::string::npos)",
        "codium-blocks-transport-smoke",
        "^transport-smoke$",
    ),
)


def run(command: list[str], cwd: Path, timeout: int, *, check: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        check=check,
    )


def copy_source(root: Path, destination: Path) -> None:
    def ignore(path: str, names: list[str]) -> set[str]:
        ignored: set[str] = set()
        for name in names:
            candidate = Path(path) / name
            if name in {".git", "CMakeFiles"} or (candidate.is_dir() and
                    (name == "build" or name.startswith("build-"))) or \
                    name.endswith((".o", ".a", ".so")):
                ignored.add(name)
        return ignored

    shutil.copytree(root, destination, ignore=ignore)


def apply_mutation(root: Path, mutation: Mutation) -> None:
    path = root / mutation.source
    text = path.read_text(encoding="utf-8")
    count = text.count(mutation.needle)
    if count != 1:
        raise RuntimeError(f"{mutation.name}: expected one mutation site, found {count}")
    path.write_text(text.replace(mutation.needle, mutation.replacement, 1), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--budget", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()
    root = args.root.resolve()
    budget = max(1, min(len(MUTATIONS), args.budget))
    selected = MUTATIONS[:budget]
    killed = 0

    with tempfile.TemporaryDirectory(prefix="codium-blocks-mutation-") as temporary:
        temporary_root = Path(temporary)
        for mutation in selected:
            mutant_root = temporary_root / mutation.name
            build_root = mutant_root / "build"
            try:
                copy_source(root, mutant_root)
                apply_mutation(mutant_root, mutation)
                configure = run([
                    "cmake", "-S", str(mutant_root), "-B", str(build_root),
                    "-DCMAKE_BUILD_TYPE=Debug",
                    "-DBUILD_TESTING=ON",
                    "-DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=OFF",
                ], mutant_root, args.timeout)
                if configure.returncode != 0:
                    print(f"mutation-smoke: configure failed for {mutation.name}\n{configure.stdout}", file=sys.stderr)
                    return 2
                build = run([
                    "cmake", "--build", str(build_root), "--target", mutation.target, "--parallel", "2",
                ], mutant_root, args.timeout)
                if build.returncode != 0:
                    print(f"MUTATION_KILLED {mutation.name} (compile failed)")
                    killed += 1
                    continue
                test = run([
                    "ctest", "--test-dir", str(build_root), "-R", mutation.test,
                    "--output-on-failure",
                ], mutant_root, args.timeout)
                if test.returncode == 0:
                    print(f"MUTATION_SURVIVED {mutation.name}")
                    print(test.stdout)
                    return 1
                print(f"MUTATION_KILLED {mutation.name}")
                killed += 1
            except subprocess.TimeoutExpired as error:
                print(f"mutation-smoke: timeout while testing {mutation.name}: {error}", file=sys.stderr)
                return 2
            except Exception as error:  # pragma: no cover - diagnostic path
                print(f"mutation-smoke: {mutation.name}: {error}", file=sys.stderr)
                return 2

    print(f"mutation-smoke: ok — killed {killed}/{len(selected)} bounded mutants")
    return 0 if killed == len(selected) else 1


if __name__ == "__main__":
    raise SystemExit(main())
