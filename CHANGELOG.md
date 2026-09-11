# Changelog

All notable changes to Codium::Blocks are documented here. The project is a development snapshot and is not yet a stable 1.0 release. Version numbers in the build identify the current development increment unless a matching Git tag and reviewed release artifact are published.

## Unreleased

- Hardened the native terminal session against POSIX short writes, `EINTR`, and `EAGAIN` by retaining pending bytes until they can be delivered.
- Added Windows Job Object ownership for ConPTY sessions so closing a terminal also closes descendant processes.
- Closed the inherited POSIX PTY master in the child and preserved existing file-status flags when enabling non-blocking I/O.
- Hardened DAP framing against malformed, overflowing, truncated, and excessively large `Content-Length` values.
- Made generic VSIX installation reject missing or malformed SHA-256 digests; the native UI now requests the expected digest before installation.
- Made protocol version 2 the canonical Extension Host contract and marked protocol version 1 as historical.
- Added release and packaging documentation improvements without enabling automatic GitHub Release publication.

## 1.0.1 development snapshot

The current development increment includes the native workbench, editor productivity, LSP/DAP foundations, isolated Code::Blocks SDK integration, terminal PTY/ConPTY support, English and Brazilian Portuguese localization, and CMake/CPack installation foundations. It remains subject to clean-environment package validation, supply-chain hardening, accessibility review, and human release review.

## 0.9.0 baseline

The 0.9.0 baseline introduced the native editor, extension-host foundation, protocol matrices, diagnostics, build sessions, debugger model, and the initial isolated Code::Blocks adapter architecture.
