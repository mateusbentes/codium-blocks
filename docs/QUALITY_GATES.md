# Automated Quality Gates

Codium::Blocks uses layered, deterministic checks to reduce the amount of mechanical review required after native or Extension Host changes. These checks are intended to catch compilation defects, malformed protocol data, memory errors, unsafe input handling, and broken project invariants before a human reviews architecture, interaction design, accessibility, and release behavior.

## Required repository checks

The platform workflows remain isolated. Linux, macOS, and Windows each configure and build the native application on their own runner, execute the portable CTest suite, validate the versioned matrix contracts, check JavaScript syntax, and run the Extension Host smoke test. External LSP/DAP execution is registered with CTest by default for local development, but platform CI disables it so missing or runner-specific servers cannot make the native build flaky; the dedicated real-toolchain workflows now run those scenarios strictly on every push, in addition to manual dispatch and weekly refreshes. The optional Code::Blocks SDK integration remains in its separate Linux workflow because it requires matched SDK resources and ABI-sensitive plugins.

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

The repository now includes bounded, offline fuzz-oriented checks. `fuzz-smoke` is a native CTest target with fixed pseudo-random generation and a configurable budget. It replays the versioned DAP corpus in `tests/corpus/dap/`, then feeds DAP `Content-Length` frames, malformed headers, and fragmented input directly through the production framer in-process, avoiding a child process for every parser case on Windows. The corpus is deliberately small and bounded: valid and invalid seeds are classified, size-limited, and replayed deterministically before generated cases run. The harness also exercises fragmented VT/ANSI input, cursor bounds, and semantic VSIX archive mutations through the production parser and installer. The generated ZIPs remain structurally valid so the result tests Codium::Blocks policy rather than platform-specific abort behavior in a third-party archive reader; unsafe archive paths and varied entry content remain covered. Each native invocation uses a process-specific temporary root and removes VSIX cases immediately, preventing concurrent runs and Windows file-operation cleanup from interfering with one another. `fuzz-host-smoke` separately starts a fresh Extension Host for each case and exercises JSON Lines requests plus valid, malformed, truncated, oversized, and mixed-case LSP headers through a controlled child process. Neither harness accesses the network or claims exhaustive coverage.

The separate `fuzz-linux.yml` workflow runs these smokes with AddressSanitizer and UndefinedBehaviorSanitizer automatically for every push and for pull requests targeting `main`, as well as through a bounded manual dispatch and a weekly schedule. It retains logs only when the job fails. The workflow is intentionally separate from the platform build workflows and does not publish releases or install the optional Code::Blocks SDK.

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

## Coverage and static analysis

`CODIUM_BLOCKS_ENABLE_COVERAGE=ON` enables GCC/Clang `--coverage` instrumentation for project targets. Coverage is intentionally a Linux/GCC gate because the generated format and toolchain behavior are stable there; it is not enabled in the portable macOS or Windows builds. The automatic `coverage-linux.yml` workflow runs on pushes, pull requests, manual dispatch, and a weekly schedule. It uses the checked-in `CMakePresets.json`, the pinned `gcovr` requirement, the complete CTest suite, and uploads XML, HTML, and text reports. Coverage percentages are evidence about exercised lines and branches, not a correctness threshold or proof of untested behavior.

The `quality-security.yml` workflow runs `clang-tidy` against a Debug compile database, generates a deterministic SPDX 2.3 source inventory, audits Node manifests and GitHub Action references, verifies required push triggers and platform isolation, and runs CodeQL's C/C++ manual build mode. Pull requests additionally receive GitHub Dependency Review with a high-severity failure threshold. The source SBOM and audit reports are uploaded as artifacts; they describe the exact checked-out tree and do not replace review of system packages, wxWidgets, OpenSSL, Node.js, Code::Blocks SDK resources, or platform runtime DLLs.

The portable CTest suite also verifies the native extension-registry contract. It parses bounded Open VSX catalog fixtures, rejects unsafe or unconfigured registry identifiers and cross-origin download URLs, validates private-registry token and signature policy inputs, compares numeric extension versions, and confirms that VSIX installation records both the digest and a value-owned compatibility report. Task tests cover the non-shell variable contract, terminal tests cover supplementary Unicode, emoji modifiers, ZWJ sequences, regional indicators, and bounded Sixel/Kitty metadata, and the DebuggerGDB provider contract smoke validates its ABI callbacks and identity tuple without loading a foreign plugin. The DAP transport smoke also verifies initialize capability parsing and adapter-specific `dataBreakpointInfo` discovery.

## Periodic real-toolchain matrix

The three `real-toolchains-*.yml` workflows are deliberately isolated by operating system. Each runs on every push, manual dispatch, and a weekly schedule, installs the versions recorded in the LSP/DAP matrices, verifies executable versions, and sets `CODIUM_BLOCKS_REQUIRE_REAL_LSP=1` and `CODIUM_BLOCKS_REQUIRE_REAL_DAP=1`. In strict mode, an unavailable required executable, an initialization failure, or an incomplete scenario is a failure rather than an optional skip. The LSP matrix explicitly records the known Windows `gopls` workspace-view limitation as an environment-scoped optional case; it is still installed, version-checked, and exercised whenever the runner exposes a workspace view. The macOS DAP job uses the runner's Apple Clang with an explicit macOS SDK path to build the debuggee, verifies the Xcode-signed `debugserver`, exports its exact path through `LLDB_DEBUGSERVER_PATH`, and places its directory on `PATH`. The Windows job uses Clang CodeView/PDB output so LLDB-DAP receives native platform debug information, downloads the official LLVM 18.1.8 Windows installer with a checked-in SHA-256, runs its silent installation with an explicit ten-minute limit, and installs the exact Python 3.10.11 runtime required by the LLVM 18 Windows adapter. It places `python310.dll` beside `lldb-dap.exe` and exposes the installer's LLDB Python modules before validating adapter startup. The DAP session uses a bounded response window for cold debugger startup and LLDB-DAP's documented `debuggerRoot` launch field. For LLVM 18 LLDB-DAP, the client sends `launch`, waits for the standard `initialized` event, then sends `configurationDone`; both responses must complete before the initial `stopped` event, and source and function breakpoint requests remain deferred until that stop. The GDB path retains its established sequential launch/configuration flow. Source breakpoints remain part of the required scenario; function-breakpoint rejection is recorded as an adapter-specific unsupported capability when an adapter advertises the feature but does not accept the request. It records data-breakpoint discovery as an adapter-specific optional capability so a valid launch, stop, stack, scope, and variable scenario is not misclassified when an adapter advertises the feature but exposes no usable data identifier. The DAP matrix explicitly marks `OpenDebugAD7` as optional because the repository does not distribute that external Windows adapter; if it is installed, its scenario is still exercised and failures are reported. The ordinary platform workflows disable the external CTest registrations and rely on these isolated jobs for real execution. This separation keeps external tool availability from becoming a hidden dependency of the native build while still providing a full push and periodic matrix signal. On Windows, the portable CTest steps exclude the `conpty` and `fuzz` labels with `-LE "conpty|fuzz"`, capture verbose CTest output and a verbose test plan, and then run the mandatory native ConPTY scenario separately. The bounded Extension Host fuzz remains covered by the dedicated fuzz workflow and is not allowed to consume the ordinary Windows CTest timeout. A ConPTY startup problem or a process-heavy fuzz run therefore cannot be hidden inside an unrelated platform test failure.

The separate `fuzz-guided-linux.yml` workflow builds the optional Clang libFuzzer DAP target and seeds it from `tests/corpus/dap/`. It restores the most recent branch-scoped corpus through the pinned GitHub cache action, saves a new run-scoped corpus key after successful fuzzing, and also preserves the corpus and crash artifacts for inspection. It runs on every push, manual dispatch, and a weekly schedule, while the bounded deterministic fuzz workflow remains the fast push and pull-request gate. The libFuzzer target can be reproduced locally with `-DCMAKE_CXX_COMPILER=clang++ -DCODIUM_BLOCKS_ENABLE_LIBFUZZER=ON`; this target is intentionally Linux/Clang-only because the project does not promise an equivalent libFuzzer runtime on MSVC or macOS.

## Package evidence

The three package workflows validate executable resources after staging or extraction, check both localization catalogs and license/changelog files, exercise startup where the platform permits it, and upload a deterministic SPDX inventory for the staged product alongside package checksums. The Windows workflow creates a deterministic Windows-compatible ZIP from a fresh `cmake --install` prefix with the repository's Python `zipfile` utility, while the macOS workflow creates a deterministic UDZO image from that prefix and validates the mounted bundle with bounded `hdiutil` retries. They run on every push, on manual dispatch, and on a weekly schedule, so package evidence does not depend on a person remembering to start a job. The inventories are product-content evidence, not signatures. Packages remain unsigned, unnotarized, and unsuitable for a public release until a human verifies clean-machine behavior, platform trust prompts, accessibility, and release provenance.

A passing automated gate is evidence about the tested behavior at a specific commit and toolchain. It is not a substitute for human review of architecture, keyboard and focus behavior, screen readers, high-DPI rendering, clean-machine installation, package signing, notarization, or product intent.

## Reproducing a failure

When an invariant fails, preserve the reported seed, iteration, platform, compiler version, and build options. Re-run the same executable from the same build directory before changing the generator. For sanitizer failures, preserve the complete stack trace and the relevant `ASAN_OPTIONS` and `UBSAN_OPTIONS` values. Changes to generated cases must be reviewed like any other test change because weaker generators can create a false sense of coverage.
