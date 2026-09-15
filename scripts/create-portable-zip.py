#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Codium::Blocks Contributors

"""Create a portable ZIP from an installed staging directory."""

from __future__ import annotations

import argparse
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="Installed staging directory")
    parser.add_argument("--output", type=Path, required=True, help="ZIP output path")
    args = parser.parse_args()

    root = args.root.resolve()
    output = args.output.resolve()
    if not root.is_dir():
        raise SystemExit(f"staging directory does not exist: {root}")
    files = sorted(path for path in root.rglob("*") if path.is_file())
    if not files:
        raise SystemExit(f"staging directory is empty: {root}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files:
            archive.write(path, path.relative_to(root).as_posix())
    print(f"portable-zip: wrote {len(files)} files to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
