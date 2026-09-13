# Automated Quality Gates

Codium::Blocks uses layered, deterministic checks to reduce the amount of mechanical review required after native or Extension Host changes. These checks are intended to catch compilation defects, malformed protocol data, memory errors, unsafe input handling, and broken project invariants before a human reviews architecture, interaction design, accessibility, and release behavior.

## Required repository checks

The platform workflows remain isolated. Linux, macOS, and Windows each configure and build the native application on their own runner, execute CTest, validate the versioned LSP and DAP matrices, check JavaScript syntax, and run the Extension Host smoke test. The optional Code::Blocks SDK integration remains in its separate Linux workflow because it requires matched SDK resources and ABI-sensitive plugins.

The ordinary portable build keeps the Code::Blocks adapter disabled unless a workflow explicitly supplies a matched SDK. This prevents optional foreign plugins from becoming an implicit dependency of the main application.

The normal local validation is:

```bash
cmake -S . -B build-quality \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=OFF
cmake --build build-quality --parallel
ctest --test-dir build-quality --output-on-failure
node --check extension-host/src/host.mjs
node tests/host-smoke.mjs
node tests/integration-matrix-smoke.mjs
```

## Strict warnings

The CMake option `CODIUM_BLOCKS_STRICT_WARNINGS=ON` adds `-Werror` for GCC and Clang targets and `/WX` for MSVC targets, in addition to the existing warning levels. It is opt-in so developers can still use a local build with a newly installed compiler or platform SDK while investigating a warning from a third-party header.

Strict-warning builds apply to Codium::Blocks targets through the common CMake warning helper. External SDKs and runtime libraries are not rebuilt with the project warning policy. A platform workflow must enable this option only after the relevant runner has demonstrated that its compiler, wxWidgets build, and native entry point are warning-clean.

## Sanitizer checks

The CMake option `CODIUM_BLOCKS_ENABLE_SANITIZERS=ON` enables AddressSanitizer and UndefinedBehaviorSanitizer for GNU and Clang targets. On MSVC it enables the compiler's AddressSanitizer support. Sanitizer builds are diagnostic builds, not distribution builds, and should use the test subset that does not require external GUI sessions or proprietary platform services.

A representative Linux command is:

```bash
cmake -S . -B build-sanitized \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=OFF \
  -DCODIUM_BLOCKS_STRICT_WARNINGS=ON \
  -DCODIUM_BLOCKS_ENABLE_SANITIZERS=ON
cmake --build build-sanitized --parallel
ASAN_OPTIONS=halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build-sanitized -R \
  '^(invariants-smoke|security-smoke|problem-model-smoke|terminal-screen-smoke)$' \
  --output-on-failure
```

Sanitizers do not prove the absence of data races, deadlocks, protocol design errors, accessibility defects, visual regressions, ABI incompatibilities, or release-signing problems. Linux and macOS use different compiler and system-library combinations, and Windows AddressSanitizer has different coverage from GNU/Clang builds. Each platform therefore retains its ordinary native CI and its own external-tool checks.

## Deterministic invariant smoke

`invariants-smoke` is a native CTest target with fixed pseudo-random seeds. It does not use the network and reports the seed and iteration when a generated case fails. The current properties cover:

| Area | Invariants exercised |
|---|---|
| Terminal screen | Feeding a VT/ANSI stream as one buffer and as deterministic fragments produces the same value-owned screen state. Cursor, modes, hyperlink state, graphics-discard state, scrollback metadata, and cells are compared. |
| Diagnostics | Generated GCC-style diagnostics normalize to zero-based positions, preserve the message, resolve workspace-relative paths, and retain build-session identity after ANSI stripping. |
| Extension manifests | Generated safe name, publisher, and version tokens are accepted; generated path-like names containing `..` are rejected. |
| Localization | English fallback remains available, Portuguese overlays English, the product name remains invariant, and unknown keys return their explicit fallback. |

These are property-style checks, not a claim of exhaustive formal verification. The seed is fixed for reproducibility, and the generated case count is intentionally bounded so the smoke remains suitable for every portable build.

## Future periodic checks

Mutation testing and long-running fuzzing are deliberately separate from the per-commit gate. Mutation testing is useful for measuring whether tests detect deliberate changes, while fuzzing is particularly valuable for DAP framing, terminal escape sequences, diagnostics, JSON/LSP input, catalog parsing, and VSIX archive boundaries. Those checks should run with bounded budgets in manually triggered or scheduled jobs after their corpus, sanitizer configuration, and failure-artifact retention are reviewed.

A passing automated gate is evidence about the tested behavior at a specific commit and toolchain. It is not a substitute for human review of architecture, keyboard and focus behavior, screen readers, high-DPI rendering, clean-machine installation, package signing, notarization, or product intent.

## Reproducing a failure

When an invariant fails, preserve the reported seed, iteration, platform, compiler version, and build options. Re-run the same executable from the same build directory before changing the generator. For sanitizer failures, preserve the complete stack trace and the relevant `ASAN_OPTIONS` and `UBSAN_OPTIONS` values. Changes to generated cases must be reviewed like any other test change because weaker generators can create a false sense of coverage.
