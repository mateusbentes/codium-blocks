# Code::Blocks SDK Integration

Codium::Blocks integrates with Code::Blocks through a **separate native host adapter process**. The main `codium-blocks` process never loads a Code::Blocks shared library or plugin into its own address space. This boundary is required because Code::Blocks plugins depend on the `Manager`, `PluginManager`, SDK globals, event routing, resources, and a matching C++ ABI.

## Implemented layers

### Safe discovery and `.cbp` import

`codium::CodeBlocksBridge` discovers a Code::Blocks installation or source root, locates SDK headers, identifies native plugin libraries, and parses sidecar `manifest.xml` files. Discovery is informational and does not call `dlopen`, `LoadLibrary`, or `wxDynamicLibrary`.

Codium::Blocks also imports a `.cbp` file in the active workspace. The importer reads the `Project/Build/Target` structure, exposes the target names in the scheme bar, records each target compiler, and creates a corresponding Code::Blocks build task. The task uses the documented command-line shape `codeblocks --build --target="Target" project.cbp` when no compatible host adapter is available.

### Isolated host adapter

When a compatible development installation is available, CMake builds an optional executable named `codium-blocks-codeblocks-adapter`. The portable core does not depend on this target. On Linux, discovery uses `pkg-config codeblocks` and verifies `resources.zip` plus the native `Compiler` plugin. Windows and macOS builds can provide explicit SDK, data, library, and plugin paths through CMake cache variables.

The adapter owns an explicit bootstrap object with the following lifecycle:

1. It starts its own `wxApp` and hidden `wxFrame`, following the Code::Blocks application lifecycle boundary [1].
2. It registers the wxWidgets file-system, image, and XRC handlers required by the Code::Blocks SDK.
3. It sets the Code::Blocks data directory and loads the official `resources.zip` archive.
4. It loads exactly one caller-selected `Compiler` plugin path. The path must be supplied explicitly and the plugin must register as Code::Blocks `Compiler` against the headers and library used to build the adapter.
5. It marks the SDK application state as started and exposes project enumeration.
6. It calls `ProjectManager::LoadProject()` and enumerates real `cbProject` build targets through `GetBuildTargetsCount()` and `GetBuildTarget()` [2].
7. It activates the matched `cbCompilerPlugin`, accepts a real target build, and drains normalized compiler lifecycle and PipedProcess output events asynchronously.
8. When requested, it creates a private staging directory containing only the matched Compiler and Debugger files from one plugin directory. It uses the SDK `ScanForPlugins()` path for that staging directory instead of loading two foreign modules through unrelated direct calls [3].
9. It installs no-op debugger window and menu factories inside the adapter, attaches the matched `cbDebuggerPlugin` through the public debugger plugin boundary [5], and exposes only debug launch, continue, break, stop, and official lifecycle events.
10. When configured, it loads a separately compiled DebuggerGDB data provider whose source revision, SDK tuple, compiler identity, language standard, build flags, and explicit ABI tag are checked before use [6] [7].
11. It explicitly unloads the matched Debugger and Compiler plugins before freeing the Code::Blocks manager; this follows the SDK ownership order and avoids leaving plugin destruction to a partially torn-down manager.

The first real capabilities are intentionally narrow and observable. Opening a `.cbp` emits `projectOpened` followed by one `projectTarget` event per real target. Each target event includes its title, compiler identifier, output path, and working directory. A build request now invokes the matched Code::Blocks Compiler plugin, emits official `buildStarted` and `buildFinished` events, forwards compiler stdout/stderr as `compilerOutput`, and preserves the SDK exit status. If a matched Debugger plugin is explicitly supplied, the adapter also starts the real debugger session for the selected target and forwards official debugger lifecycle events. The public debugger delivery adds a value-owned state snapshot. The optional private-provider delivery adds a separately compiled DebuggerGDB provider that serializes stack frames, threads, breakpoints, watches, and expression-backed variables into value-owned protocol data. The provider is unavailable unless its exact source and ABI identity are supplied; the generic adapter continues to reject private model data explicitly.

The unified build layer consumes these adapter targets through the same native `ProjectTarget` and `ProjectScheme` model used by CMake, Make, Cargo, and npm. A selected Code::Blocks scheme supplies the `.cbp` path and target title to the adapter, and each invocation is recorded in the persistent Build-session history alongside its raw compiler output and normalized Problems. This keeps the adapter-specific ABI boundary separate from the IDE's portable Build and Problems models.

### Event normalization

The adapter now installs typed event sinks through `Manager::RegisterEventSink()` after the SDK manager is created. Project lifecycle events (`cbEVT_PROJECT_OPEN`, `cbEVT_PROJECT_CLOSE`, `cbEVT_PROJECT_ACTIVATE`, `cbEVT_PROJECT_SAVE`, target changes, and project file changes) are converted immediately into value-owned normalized events. The adapter never sends `cbProject*`, `cbPlugin*`, or other SDK pointers across JSON Lines. It also registers `cbEVT_COMPILER_STARTED` and `cbEVT_COMPILER_FINISHED`, preserving the active project, target, Compiler identity, exit code, and error status.

The event layer observes and normalizes official SDK events before they cross the process boundary. The dedicated SDK event smoke test drives the official `Manager::ProcessEvent()` path with SDK event objects and verifies that project file changes and compiler success/failure statuses reach the normalized model. The real adapter smoke additionally compiles a small C++ fixture, verifies a real executable artifact, checks `cbEVT_COMPILER_STARTED`/`cbEVT_COMPILER_FINISHED`, and captures a deterministic compiler warning through `compilerOutput`.

The repository includes a Linux integration smoke test that starts the adapter under `xvfb-run`, negotiates the protocol, opens a fixture `.cbp`, verifies the real `Debug` and `Release` targets returned by the installed Code::Blocks SDK, and builds the fixture through the matched Compiler plugin. It checks the produced executable, official compiler lifecycle events, and captured warning output. The test is not added when a usable SDK, runtime resources, compiler plugin, or virtual display is unavailable. The universal fake-adapter test remains independent of Code::Blocks and continues to run on every platform.

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
| Third-party plugins | Do not load arbitrary discovered plugins. Only the explicit Compiler and optional Debugger paths are eligible, and a debugger request requires workspace trust. |
| Build behavior | Invoke only the matched Compiler plugin; preserve asynchronous output and completion status, and never emit fake success. |
| Debugger behavior | Attach only the matched Debugger plugin from the same installation. Provide headless SDK interfaces and normalize session lifecycle. Expose public state in the generic adapter. Load private DebuggerGDB data only through the separately compiled E.2 provider after exact identity checks. |

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
ctest --test-dir build -N
ctest --test-dir build -R '^codeblocks-(real-adapter|sdk-events)-smoke$' --output-on-failure
```

The real adapter and SDK event tests are registered only when CMake finds `xvfb-run`, the configured Code::Blocks data directory containing `resources.zip`, and the matched Compiler plugin. Inspect the `ctest -N` output before running a filtered command. If either test is absent, the build is a portable/fake-adapter build rather than a failed real-SDK validation; install the prerequisites below and configure again instead of treating “No tests were found” as success.

On Debian or Ubuntu, the usual prerequisites are:

```bash
sudo apt-get install build-essential cmake pkg-config libwxgtk3.2-dev \
  codeblocks codeblocks-dev libtinyxml-dev xvfb
pkg-config --modversion codeblocks
```

`codeblocks-dev` supplies SDK headers, the shared library, and `pkg-config` metadata. `codeblocks` supplies official runtime resources and plugins. `libtinyxml-dev` supplies the `tinyxml.h` header used by SDK headers, and `xvfb` supplies the virtual display used by the headless adapter smoke. These packages are not required by the portable native IDE.

For installations without a usable `pkg-config` file, provide a Code::Blocks root and, when necessary, explicit runtime paths:

```bash
cmake -S . -B build \
  -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_ADAPTER=ON \
  -DCODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT=/path/to/codeblocks \
  -DCODIUM_BLOCKS_CODEBLOCKS_DATA_DIR=/path/to/share/codeblocks \
  -DCODIUM_BLOCKS_CODEBLOCKS_PLUGIN_DIR=/path/to/codeblocks/plugins \
  -DCODIUM_BLOCKS_CODEBLOCKS_COMPILER_PLUGIN=/path/to/codeblocks/plugins/compiler-module \
  -DCODIUM_BLOCKS_CODEBLOCKS_DEBUGGER_PLUGIN=/path/to/codeblocks/plugins/debugger-module
```

The explicit `COMPILER_PLUGIN` path is required when the installation uses a non-standard module name. The `DEBUGGER_PLUGIN` path is optional. If it is omitted, the adapter remains a real compiler/project host and does not claim debugger capabilities.

The private DebuggerGDB provider is an opt-in Linux-oriented integration. It must be compiled from the same Code::Blocks source revision and with the same SDK and compiler ABI as the installed Debugger plugin. The following configuration enables the provider after the matching source tree has been prepared:

```bash
cmake -S . -B build-provider \
  -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_DEBUGGERGDB_PROVIDER=ON \
  -DCODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_DIR=/path/to/src/plugins/debuggergdb \
  -DCODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_SOURCE_REVISION=r13046 \
  -DCODIUM_BLOCKS_CODEBLOCKS_DEBUGGERGDB_ABI_TAG=distribution-source-compiler-wx-build-id
cmake --build build-provider
```

`SOURCE_REVISION` and `ABI_TAG` are required evidence fields, not guesses. The tag should identify the distribution, compiler version, wxWidgets build, architecture, language standard, and relevant build flags. If any private source or identity field is missing, CMake disables the provider and still builds the portable adapter. The provider is never enabled merely because a file named `libdebugger.so` exists.

The older `CODIUM_BLOCKS_ENABLE_CODEBLOCKS_SDK` option still exposes optional SDK include roots to the portable native target. It does not link the core application against `libcodeblocks` and does not enable plugin loading.

## Protocol and capability reporting

The native client and adapter use the versioned JSON Lines contract in [`CODEBLOCKS_ADAPTER_PROTOCOL.md`](CODEBLOCKS_ADAPTER_PROTOCOL.md) [4]. The contract is now `1.3`; clients that speak `1.0`, `1.1`, or `1.2` remain compatible because provider metadata and debugger data requests are additive. A `ready` response reports the compiled SDK version, a human-readable SDK identity, and capabilities such as `sdkBootstrap`, `projectEvents`, `projectTargets`, `compilerEvents`, `compilerBuild`, `compilerOutput`, `compilerPluginMatched`, `debuggerPluginMatched`, `debuggerEvents`, `debuggerControl`, `debuggerSnapshot`, `debuggerPublicState`, and `debuggerDataUnavailable`. A provider-enabled response additionally reports `debuggerPrivateProvider`, `debuggerStackFrames`, `debuggerThreads`, `debuggerBreakpoints`, `debuggerWatches`, and `debuggerVariables`, together with provider identity fields.

A handshake with SDK version `0.0.0` requests capability discovery without imposing a version. A non-zero requested SDK tuple must match exactly. A contract major mismatch or unsupported minor version is rejected before project operations begin.

## Verified adapter capabilities

The adapter is deliberately evaluated by observable capabilities rather than by historical stage names:

| Capability | Status | Evidence |
|---|---|---|
| SDK and resource bootstrap | Implemented | Matched `wxApp` and resource loading are exercised by the Linux SDK integration job. |
| Project and target import | Implemented | Real `.cbp` loading and target enumeration are validated through the adapter smoke test. |
| Compiler lifecycle and builds | Implemented | Official compiler events, output capture, completion status, and a produced executable are verified. |
| Debugger lifecycle | Implemented | Matched Debugger plugin launch, control, and official lifecycle events are tested when the optional installation is available. |
| Public debugger boundary | Implemented | Public state is transferred as value-owned data, while unavailable private data returns an explicit capability response. |
| Private DebuggerGDB provider | Opt-in and implemented | An exact source revision and ABI identity are validated before value-owned frames, threads, breakpoints, watches, and variables are exposed. |
| Native debugger views | Implemented | The main process renders provider snapshots without linking private provider headers. |
| Active-frame synchronization | Implemented | Stopped events and manual frame selection map source locations into native editor tabs and caret positions. |

The adapter never exposes private Code::Blocks objects to the main process. It consumes and emits only JSON Lines snapshots and normalized events, so the portable IDE and the generic adapter remain independent of private DebuggerGDB headers. Full Code::Blocks debugger and plugin compatibility is not implied; every additional capability still requires a matched SDK test and an explicit trust policy.

## References

1. [Code::Blocks application lifecycle](https://svn.code.sf.net/p/codeblocks/code/trunk/src/src/app.cpp)
2. [Code::Blocks ProjectManager implementation](https://svn.code.sf.net/p/codeblocks/code/trunk/src/sdk/projectmanager.cpp)
3. [Code::Blocks PluginManager implementation](https://svn.code.sf.net/p/codeblocks/code/trunk/src/sdk/pluginmanager.cpp)
4. [Codium::Blocks host adapter protocol](https://github.com/mateusbentes/codium-blocks/blob/main/docs/CODEBLOCKS_ADAPTER_PROTOCOL.md)
5. [Code::Blocks public debugger plugin interface](https://svn.code.sf.net/p/codeblocks/code/trunk/src/include/cbplugin.h)
6. [Code::Blocks DebuggerGDB private data definitions](https://svn.code.sf.net/p/codeblocks/code/trunk/src/plugins/debuggergdb/debugger_defs.h)
7. [Code::Blocks DebuggerGDB private driver containers](https://svn.code.sf.net/p/codeblocks/code/trunk/src/plugins/debuggergdb/debuggerdriver.h)

[1]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/src/app.cpp "Code::Blocks application lifecycle"
[2]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/sdk/projectmanager.cpp "Code::Blocks ProjectManager implementation"
[3]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/sdk/pluginmanager.cpp "Code::Blocks PluginManager implementation"
[4]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/CODEBLOCKS_ADAPTER_PROTOCOL.md "Codium::Blocks host adapter protocol"
[5]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/include/cbplugin.h "Code::Blocks public debugger plugin interface"
[6]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/plugins/debuggergdb/debugger_defs.h "Code::Blocks DebuggerGDB private data definitions"
[7]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/plugins/debuggergdb/debuggerdriver.h "Code::Blocks DebuggerGDB private driver containers"
