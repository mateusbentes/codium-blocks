# Licensing and Third-Party Provenance

Codium::Blocks source code in this repository is licensed under **GNU General Public License v3.0 only (GPL-3.0-only)**. `LICENSE` and `COPYING` contain the complete license text. Source files carry SPDX headers so automated scanners can identify the intended license.

## Repository components

| Component | Location or role | License/provenance policy |
|---|---|---|
| Native workbench and tests | `native/` and `tests/` | GPL-3.0-only, with SPDX headers. |
| Node.js Extension Host | `extension-host/` | GPL-3.0-only; Node.js itself remains an external runtime under its own terms. |
| Demonstration extension | `extensions/hello-codium/` | GPL-3.0-only; it is a first-party validation extension, not a compatibility promise for arbitrary VS Code extensions. |
| Protocol documentation | `protocol/` and `docs/` | Project documentation under GPL-3.0-only unless a file states otherwise. Protocol names, JSON keys, and LSP/DAP identifiers remain technical identifiers. |
| wxWidgets | Build dependency | External system dependency. Its upstream license and notices must be retained by the distribution that ships it. |
| OpenSSL | Optional signature-verification dependency | External system dependency. Its upstream license and notices apply when linked or redistributed. |
| Node.js | Optional Extension Host runtime | External runtime. A normal native build does not require Electron or a bundled Node.js runtime. |
| Code::Blocks SDK and plugins | Optional isolated adapter integration | External GPL project and installed resources. The normal portable package does not load or bundle Code::Blocks libraries. A matching SDK, resources, plugins, and ABI are required for the optional adapter. |
| DebuggerGDB provider source | Optional Linux CI/provider build | External Code::Blocks source snapshot. The CI fixes a source revision and builds it only for the opt-in provider; provenance and license notices must accompany any future redistribution. |

This table is an inventory policy, not a substitute for the license files distributed by upstream projects. Before publishing a binary release, generate an SBOM and review the exact dependency versions, licenses, notices, and runtime files included in each platform artifact.

## Distribution requirements

A release artifact must include the project license and copyright notices. It must not imply that third-party dependencies are relicensed as GPL-3.0-only. The release process must record the source revision, compiler/toolchain identity, dependency versions, package contents, checksums, and any signatures or attestations.

The Code::Blocks adapter remains out of process. Standard packages build it disabled, so users who only use the portable IDE do not receive an implicit Code::Blocks SDK dependency. If a future package includes the adapter, its package manifest must identify the additional Code::Blocks components and preserve their notices.

The project does not currently claim signed or notarized release artifacts. SHA-256 checksums verify bytes after transfer but do not replace platform code signing, notarization, repository signatures, or provenance attestations.

## Contributor procedure

New project-owned source files must include:

```text
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
```

Imported, generated, or third-party files must retain their original license headers and must be listed separately in this document or in a generated release SBOM. Do not copy code into the repository without recording its origin, applicable license, and required notices.
