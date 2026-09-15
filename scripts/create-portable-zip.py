#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Codium::Blocks Contributors

"""Create a portable ZIP from an installed staging directory."""

from __future__ import annotations

import argparse
import os
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
    files = []
    for directory, directory_names, file_names in os.walk(root, followlinks=False):
        directory_names[:] = sorted(
            name for name in directory_names
            if not os.path.islink(os.path.join(directory, name))
        )
        files.extend(
            Path(directory, name)
            for name in sorted(file_names)
            if not os.path.islink(os.path.join(directory, name))
        )
    files.sort(key=lambda path: path.relative_to(root).as_posix())
    if not files:
        raise SystemExit(f"staging directory is empty: {root}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    with ZipFile(output, "w", compression=ZIP_DEFLATED, compresslevel=6, strict_timestamps=False) as archive:
        for path in files:
            archive_name = path.relative_to(root).as_posix()
            try:
                archive.write(path, archive_name)
            except (OSError, ValueError) as error:
                raise SystemExit(f"portable ZIP could not read {path}: {error}") from error
    with ZipFile(output, "r") as archive:
        corrupt = archive.testzip()
    if corrupt is not None:
        raise SystemExit(f"portable ZIP failed integrity check at: {corrupt}")
    print(f"portable-zip: wrote {len(files)} files to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
