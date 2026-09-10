# Code::Blocks SDK Integration

Codium::Blocks integrates with Code::Blocks through a **separate native host adapter process**. The main `codium-blocks` process never loads a Code::Blocks shared library or plugin into its own address space. This boundary is required because Code::Blocks plugins depend on the `Manager`, `PluginManager`, SDK globals, event routing, resources, and a matching C++ ABI.

## Implemented layers

### Safe discovery and `.cbp` import

`codium::CodeBlocksBridge` discovers a Code::Blocks installation or source root, locates SDK headers, identifies native plugin libraries, and parses sidecar `manifest.xml` files. Discovery is informational and does not call `dlopen`, `LoadLibrary`, or `wxDynamicLibrary`.

Codium::Blocks also imports a `.cbp` file in the active workspace. The importer reads the `Project/Build/Target` structure, exposes the target names in the scheme bar, records each target compiler, and creates a corresponding Code::Blocks build task. The task uses the documented command-line shape `codeblocks --build --target="Target" project.cbp` when no compatible host adapter is available.

### Isolated Phase A host adapter

When a compatible development installation is available, CMake builds an optional executable named `codium-blocks-codeblocks-adapter`. The portable core does not depend on this target. On Linux, discovery uses `pkg-config codeblocks` and verifies `resources.zip` plus the native `Compiler` plugin. Windows and macOS builds can provide explicit SDK, data, library, and plugin paths through CMake cache variables.

The adapter owns an explicit bootstrap object with the following lifecycle:

1. It starts its own `wxApp` and hidden `wxFrame`.
2. It registers the wxWidgets file-system, image, and XRC handlers required by the Code::Blocks SDK.
3. It sets the Code::Blocks data directory and loads the official `resources.zip` archive.
4. It loads exactly one caller-selected `Compiler` plugin path. The path must be supplied explicitly and the plugin must register as Code::Blocks `Compiler` against the headers and library used to build the adapter.
5. It marks the SDK application state as started and exposes project enumeration.
6. It calls `ProjectManager::LoadProject()` and enumerates real `cbProject` build targets through `GetBuildTargetsCount()` and `GetBuildTarget()`.
7. It closes projects and frees the Code::Blocks manager before the adapter process exits.

The first real capability is intentionally modest and observable. Opening a `.cbp` emits `projectOpened` followed by one `projectTarget` event per real target. Each target event includes its title, compiler identifier, output path, and working directory. Build requests are not yet implemented by this phase; the adapter reports them as unsupported rather than pretending to provide a real compiler event stream.

The repository includes a Linux integration smoke test that starts the adapter under `xvfb-run`, negotiates the protocol, opens a fixture `.cbp`, and verifies the real `Debug` and `Release` targets returned by the installed Code::Blocks SDK. The test is not added when a usable SDK, runtime resources, compiler plugin, or virtual display is unavailable. The universal fake-adapter test remains independent of Code::Blocks and continues to run on every platform.

## Plugin and ABI safety policy

Code::Blocks plugins are not standalone modules with a stable, host-independent ABI. A plugin registers through `PluginRegistrant<T>`, creates `cbPlugin` objects, and expects the Code::Blocks manager graph and SDK-specific global infrastructure to exist. The plugin manager also validates the embedded resource manifest and SDK version.

The current adapter therefore follows these rules:

| Rule | Current behavior |
|---|---|
| Main-process loading | Never load Code::Blocks libraries or plugins in `codium-blocks`. |
| Process boundary | Run the SDK and its selected native plugin only in `codium-blocks-codeblocks-adapter`. |
| Plugin selection | Require an explicit compiler-plugin path. Do not scan a plugin directory. |
| ABI check | Require the plugin's embedded SDK version to match the adapter's compiled SDK. |
| Resource check | Require the configured data directory to contain the official `resources.zip`. |
| Third-party plugins | Do not load arbitrary discovered plugins. Plugin attach and plugin command events remain future work. |
| Build behavior | Do not emit fake build success. Phase A reports build as unsupported. |

The process boundary limits a plugin crash to the adapter process, but it does not make arbitrary native plugins trustworthy. Workspace trust, installation provenance, and a future explicit allowlist remain required before any broader plugin policy is considered.

## CMake configuration

The default build remains independent of Code::Blocks:

```bash
cmake -S . -B build
cmake --build build
```

To enable automatic SDK discovery where `pkg-config codeblocks` is available:

```bash
cmake -S . -B build \
  -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=ON \
  -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build -R codeblocks-real-adapter-smoke --output-on-failure
```

For installations without a usable `pkg-config` file, provide a Code::Blocks root and, when necessary, explicit runtime paths:

```bash
cmake -S . -B build \
  -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=ON \
  -DCODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT=/path/to/codeblocks \
  -DCODIUM_BLOCKS_CODEBLOCKS_DATA_DIR=/path/to/share/codeblocks \
  -DCODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR=/path/to/codeblocks/plugins
```

The older `CODIUM_BLOCKS_ENABLE_CODEBLOCKS_SDK` option still exposes optional SDK include roots to the portable native target. It does not link the core application against `libcodeblocks` and does not enable plugin loading.

## Protocol and capability reporting

The native client and adapter use the versioned JSON Lines contract in [`CODEBLOCKS_ADAPTER_PROTOCOL.md`](CODEBLOCKS_ADAPTER_PROTOCOL.md). The contract remains `1.0` for this compatible Phase A addition. A `ready` response reports the compiled SDK version, a human-readable SDK identity, and capabilities such as `sdkBootstrap`, `projectEvents`, `projectTargets`, and `compilerPluginMatched`.

A handshake with SDK version `0.0.0` requests capability discovery without imposing a version. A non-zero requested SDK tuple must match exactly. A contract major mismatch or unsupported minor version is rejected before project operations begin.

## Staged production plan

The production adapter is deliberately being developed in stages:

| Stage | Status | Scope |
|---|---|---|
| A | Implemented | Matched `wxApp`/resource bootstrap, one allowlisted Compiler plugin, real `.cbp` loading, target enumeration, capability reporting, and graceful errors. |
| B | Next | Register official SDK event sinks and normalize project/compiler lifecycle events without loading additional plugins. |
| C | Planned | Add real batch compilation through the matched Code::Blocks compiler plugin, including compiler output and completion status. |
| D | Planned | Add debugger and plugin-manager capabilities only behind explicit ABI, manifest, provenance, and workspace-trust policies. |

This sequencing avoids claiming full Code::Blocks compatibility before the lifecycle, event mapping, compiler behavior, debugger ownership, and plugin policy have each been tested against matched SDK builds.

## References

[1]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/src/app.cpp "Code::Blocks application lifecycle"
[2]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/sdk/projectmanager.cpp "Code::Blocks ProjectManager implementation"
[3]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/sdk/pluginmanager.cpp "Code::Blocks PluginManager implementation"
[4]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/CODEBLOCKS_ADAPTER_PROTOCOL.md "Codium::Blocks host adapter protocol"
