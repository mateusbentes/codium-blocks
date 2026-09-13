#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Codium::Blocks Contributors

"""Generate a small deterministic SPDX 2.3 inventory for a Codium::Blocks tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from datetime import datetime, timezone
from pathlib import Path

SKIP_PARTS = {
    ".git", ".quality-venv", ".venv", "__pycache__", "artifacts", "build", "build-fuzz",
    "build-sanitized", "build-libfuzzer", "coverage", "node_modules",
}
LICENSE_FILES = {"LICENSE", "COPYING"}


def iter_files(root: Path):
    for path in sorted(root.rglob("*")):
        if not path.is_file() or any(part in SKIP_PARTS for part in path.relative_to(root).parts):
            continue
        yield path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def created_time() -> str:
    epoch = os.environ.get("SOURCE_DATE_EPOCH", "0")
    try:
        value = datetime.fromtimestamp(int(epoch), tz=timezone.utc)
    except (TypeError, ValueError, OSError):
        value = datetime(1970, 1, 1, tzinfo=timezone.utc)
    return value.isoformat().replace("+00:00", "Z")


def project_version(root: Path) -> str:
    cmake = root / "CMakeLists.txt"
    if cmake.is_file():
        match = re.search(r"project\(CodiumBlocks VERSION ([^ )]+)", cmake.read_text(encoding="utf-8"))
        if match:
            return match.group(1)
    return "NOASSERTION"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--name", default="Codium::Blocks source tree")
    parser.add_argument("--version", help="Project version when --root is an installed tree")
    args = parser.parse_args()
    root = args.root.resolve()
    version = args.version or project_version(root)
    files = list(iter_files(root))
    document_id = "SPDXRef-DOCUMENT"
    project_id = "SPDXRef-Package-CodiumBlocks"
    packages = [{
        "SPDXID": project_id,
        "name": "Codium::Blocks",
        "versionInfo": version,
        "downloadLocation": "NOASSERTION",
        "filesAnalyzed": True,
        "licenseConcluded": "GPL-3.0-only",
        "licenseDeclared": "GPL-3.0-only",
        "copyrightText": "Copyright (C) 2026 Codium::Blocks Contributors",
        "externalRefs": [{
            "referenceCategory": "PACKAGE-MANAGER",
            "referenceType": "purl",
            "referenceLocator": "pkg:github/mateusbentes/codium-blocks"
        }]
    }]
    files_json = []
    relationships = []
    for index, path in enumerate(files):
        relative = path.relative_to(root).as_posix()
        file_id = f"SPDXRef-File-{index + 1}"
        files_json.append({
            "SPDXID": file_id,
            "fileName": relative,
            "checksums": [{"algorithm": "SHA256", "checksumValue": sha256(path)}],
            "licenseConcluded": "GPL-3.0-only" if Path(relative).name in LICENSE_FILES else "NOASSERTION",
            "copyrightText": "NOASSERTION"
        })
        relationships.append({
            "spdxElementId": project_id,
            "relationshipType": "CONTAINS",
            "relatedSpdxElement": file_id
        })
    document = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": document_id,
        "name": args.name,
        "documentNamespace": f"https://spdx.org/spdxdocs/codium-blocks-source-{version}",
        "creationInfo": {
            "created": created_time(),
            "creators": ["Tool: codium-blocks-generate-sbom/1.0"]
        },
        "packages": packages,
        "files": files_json,
        "relationships": relationships
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"sbom: wrote {len(files_json)} files to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
