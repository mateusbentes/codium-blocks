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

## Bounded fuzzing and mutation checks

The repository now includes bounded, offline fuzz-oriented checks. `fuzz-smoke` is a native CTest target with fixed pseudo-random generation and a configurable budget. It exercises DAP `Content-Length` framing, fragmented VT/ANSI input, cursor bounds, and semantic VSIX archive mutations through the production parser and installer. The generated ZIPs remain structurally valid so the result tests Codium::Blocks policy rather than platform-specific abort behavior in a third-party archive reader; unsafe archive paths and varied entry content remain covered. Its DAP child is a finite fixture: it emits one payload, closes its output, and exits naturally, so repeated Windows iterations do not accumulate live Node processes that require force termination. The DAP harness also treats an adapter closing its IPC pipe during teardown as an ordinary protocol failure instead of allowing `SIGPIPE` to terminate the test process. Each native invocation uses a process-specific temporary root and removes DAP payloads and VSIX cases immediately, preventing concurrent runs and Windows file-operation cleanup from interfering with one another. `fuzz-host-smoke` starts a fresh Extension Host for each case and exercises JSON Lines requests plus valid, malformed, truncated, oversized, and mixed-case LSP headers through a controlled child process. Neither harness accesses the network or claims exhaustive coverage.

The separate `fuzz-linux.yml` workflow runs these smokes with AddressSanitizer and UndefinedBehaviorSanitizer automatically for pushes to `main` and pull requests targeting `main`, as well as through a bounded manual dispatch and a weekly schedule. It retains logs only when the job fails. The workflow is intentionally separate from the platform build workflows and does not publish releases or install the optional Code::Blocks SDK.

The bounded mutation check is `tests/mutation-smoke.py`. It creates disposable source copies, applies three exact mutations to terminal carriage return, the HTTPS registry allowlist, and DAP header detection, and requires the corresponding existing smoke to fail. A surviving mutant is a hard failure. This is a small test-strength signal, not a mutation score for the entire codebase:

```bash
python3 tests/mutation-smoke.py --budget 3 --timeout 120
```

The fuzz smoke can be replayed with a larger but still bounded local budget:

```bash
CODIUM_BLOCKS_FUZZ_ITERATIONS=512 \
  ctest --test-dir build-fuzz -R '^(fuzz-smoke|fuzz-host-smoke)$' --output-on-failure
```

Mutation testing and fuzzing do not prove the absence of data races, deadlocks, protocol design errors, accessibility defects, visual regressions, ABI incompatibilities, or release-signing problems. The existing deterministic invariant smoke remains the fast per-build property-style check; these fuzz and mutation jobs add bounded depth without replacing platform CI or human review.

A passing automated gate is evidence about the tested behavior at a specific commit and toolchain. It is not a substitute for human review of architecture, keyboard and focus behavior, screen readers, high-DPI rendering, clean-machine installation, package signing, notarization, or product intent.

## Reproducing a failure

When an invariant fails, preserve the reported seed, iteration, platform, compiler version, and build options. Re-run the same executable from the same build directory before changing the generator. For sanitizer failures, preserve the complete stack trace and the relevant `ASAN_OPTIONS` and `UBSAN_OPTIONS` values. Changes to generated cases must be reviewed like any other test change because weaker generators can create a false sense of coverage.
