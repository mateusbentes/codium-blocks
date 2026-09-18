# Licensing and Third-Party Provenance

## Short answer

The **Codium::Blocks main project** is licensed under **GNU General Public License v3.0 only (`GPL-3.0-only`)**. This covers the project-owned core and the first-party code maintained in this repository.

This license does **not** mean that every extension compatible with Codium::Blocks must be GPL. Independent third-party extensions may remain under the license chosen by their authors, including MIT, BSD, Apache-2.0, or a proprietary license, provided that the extension is distributed as an independent work and does not incorporate GPL-covered code in a way that creates a combined program.

> The repository license governs the Codium::Blocks project. It does not automatically relicense independent extensions, external runtimes, or third-party dependencies.

This document describes the project's licensing policy and technical boundaries. It is not a legal opinion about a particular distribution model. When an extension links directly to internal GPL code, embeds copied project code, or uses a non-standard integration mechanism, the distributor should obtain specific legal advice before publishing it.

## Why the core is GPL-3.0-only

The core remains GPL-3.0-only as a deliberate project decision. Codium::Blocks is a native continuation of the Code::Blocks-oriented architecture and includes native workbench, project, compiler, debugger, terminal, adapter, and integration code intended to remain free software. The GPL preserves users' rights to study, modify, and redistribute the main application and ensures that distributed modified versions retain those freedoms.

The Code::Blocks connection does not permit this repository to relicense copied third-party files arbitrarily. Any Code::Blocks source, SDK file, plugin, generated file, or other imported material must retain its original notices and applicable license. The project-owned adapter and bridge code remains covered by this repository's license, while the optional Code::Blocks SDK and installed plugins remain external components with their own provenance.

`GPL-3.0-only` means exactly version 3 of the GNU GPL. It does not mean `GPL-3.0-or-later`. The complete terms are in [`LICENSE`](../LICENSE) and [`COPYING`](../COPYING), and the official license text is available from the Free Software Foundation.[4]

## What the project license covers

The following first-party components are GPL-3.0-only unless a file carries a more specific notice identifying an imported or generated work:

| Component | Location | Policy |
|---|---|---|
| Native workbench, models, adapters, and tests | `native/` and `tests/` | GPL-3.0-only, with SPDX headers. |
| Node.js Extension Host implementation | `extension-host/` | GPL-3.0-only. Node.js itself remains an external runtime under its own license. |
| First-party demonstration extension | `extensions/hello-codium/` | GPL-3.0-only. It is a project-owned validation extension, not a license requirement for external extensions. |
| Project documentation and protocol descriptions | `docs/` and `protocol/` | GPL-3.0-only unless an individual file states otherwise. Protocol names, JSON keys, and LSP/DAP identifiers are technical identifiers, not project-owned code. |
| Project build and validation scripts | `scripts/` and project-owned workflow support | GPL-3.0-only unless an imported file states otherwise. |

A third-party file must not be relicensed as GPL merely because it is stored in the repository. Its original copyright, license text, SPDX identifier, and required attribution must be preserved and recorded in the release SBOM or in this document.

## Independent extensions are allowed

Codium::Blocks is designed to support extensions through a public host contract, JSON Lines messages, documented contribution metadata, and out-of-process execution. That architecture permits an independently developed extension to retain its own license.

The following license categories are acceptable for an independent extension when their terms are followed:

| Extension license | Can remain under its own license? | Required care |
|---|---:|---|
| MIT or Expat | Yes | Preserve the copyright and permission notice. |
| BSD-2-Clause or BSD-3-Clause | Yes | Preserve the copyright, license text, and disclaimer. |
| Apache-2.0 | Yes | Preserve the license, notices, and any required `NOTICE` file. |
| GPL-compatible copyleft license | Usually | Check compatibility and comply with the license of every combined component. |
| Proprietary or commercial license | Potentially | Keep the extension independent, distribute its own terms, and do not copy or link to GPL-covered project code without permission. |

MIT, BSD, and Apache-2.0 are permissive licenses. They may be used for an independent extension without changing the Codium::Blocks core license. The Free Software Foundation lists permissive licenses such as the Expat/MIT and X11 licenses as compatible with GPLv3.[3] If code under one of those licenses is copied into the GPL-covered core, the combined core distribution must comply with the GPL and preserve the third-party notices.

A proprietary extension may be distributed separately and may communicate with Codium::Blocks through the documented extension protocol. It may not copy GPL-covered implementation code, link directly against GPL-covered internal libraries, or use a private integration that turns the extension and the core into one combined program while attempting to impose a proprietary license on the combined work.

Out-of-process execution is an important architectural boundary, but it is not an automatic legal guarantee. The relationship depends on how the extension is invoked and how the two programs communicate. Simple protocol communication between separate processes generally supports separation; intimate sharing of complex internal data structures, direct dynamic linking, or copied internal code can produce a different result. The GNU GPL FAQ discusses this distinction for plugins and combined programs.[1] [2]

## First-party versus third-party extensions

The repository's `hello-codium` extension is first-party code and is GPL-3.0-only because the project controls and distributes it as part of the source tree. An external author does not have to change an MIT, BSD, Apache-2.0, or proprietary extension to GPL merely because it runs in the Codium::Blocks Extension Host.

An extension author remains responsible for:

1. selecting and publishing the extension's own license;
2. preserving the licenses and notices of the extension's dependencies;
3. declaring the license in its manifest or distribution metadata when supported;
4. avoiding copied GPL-covered code unless the extension is licensed and distributed accordingly;
5. respecting the terms of any registry, service, SDK, runtime, or proprietary API used by the extension; and
6. providing any source, notices, or attribution required by the extension's own license.

Codium::Blocks may report compatibility, origin, digest, signature status, and trust policy for an extension. Those checks do not change the extension's copyright or license, and a compatibility report is not a license grant.

## External dependencies and adapters

External components keep their own licenses. The project does not relicense them as GPL-3.0-only:

| Component | Role | Licensing rule |
|---|---|---|
| wxWidgets | Native build dependency | Retain the upstream license and notices when distributing it. |
| OpenSSL | Optional signature-verification dependency | Apply the upstream license and notices when linked or redistributed. |
| Node.js | Optional Extension Host runtime | Treat the runtime as an external component under its own terms. |
| Code::Blocks SDK and plugins | Optional isolated adapter integration | Keep the matching upstream licenses, notices, resources, and ABI provenance. Standard portable packages do not bundle them. |
| DebuggerGDB provider source | Optional Linux provider build | Record the fixed Code::Blocks source revision and preserve its upstream provenance before any redistribution. |

The normal portable package keeps the Code::Blocks adapter disabled and does not bundle Code::Blocks libraries. If a future package enables or includes that adapter, its manifest and SBOM must identify every additional Code::Blocks component and preserve its notices.

Before publishing a binary artifact, the release process must inspect the exact dependency versions, licenses, notices, runtime files, and generated SPDX inventory for that artifact. A checksum proves the bytes received; it does not relicense a dependency and does not replace signing, notarization, or provenance attestations.

## Distribution requirements

A Codium::Blocks distribution must include the project license and applicable copyright notices. A distributor must not describe all files in a package as GPL-3.0-only when the package contains MIT, BSD, Apache-2.0, proprietary, or other third-party components.

A release record should identify the source revision, compiler and toolchain, dependency versions, package contents, checksums, signatures or attestations when available, and the location of the corresponding license notices. The project currently does not claim that its artifacts are signed or notarized.

For an extension registry or private distribution channel, the extension's own license should be visible before installation whenever the metadata is available. Digest verification, optional signature verification, workspace trust, and compatibility reporting are security and product controls; none of them changes the extension's license.

## Contributor procedure

New project-owned source files must include the project SPDX identifier and copyright notice:

```text
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
```

Imported, generated, or third-party files must retain their original license headers. Contributors must record the origin, applicable license, and required notices before adding such material. Do not copy code from a GPL, MIT, BSD, Apache, or proprietary project into the Codium::Blocks core without first checking the resulting license obligations.

## References

[1]: https://www.gnu.org/licenses/gpl-faq.html#GPLPlugins "GNU GPL FAQ: When is a program and its plug-ins considered a single combined program?"
[2]: https://www.gnu.org/licenses/gpl-faq.html#GPLAndPlugins "GNU GPL FAQ: What requirements apply to a plug-in for a GPL-covered program?"
[3]: https://www.gnu.org/licenses/license-list.html "Free Software Foundation license list and GPL compatibility notes"
[4]: https://www.gnu.org/licenses/gpl-3.0.html "GNU General Public License version 3"

See also [`docs/EXTENSIONS_SECURITY.md`](EXTENSIONS_SECURITY.md) for extension verification, registry policy, and workspace-trust controls.
