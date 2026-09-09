# Code::Blocks SDK Integration

Codium::Blocks now contains a native **Code::Blocks bridge**. The bridge is deliberately introduced in two stages: discovery and pre-validation come first; native plugin execution will only be enabled after a compatible Code::Blocks host ABI has been attached.

## What is implemented

`codium::CodeBlocksBridge` can discover a Code::Blocks installation or source root, locate SDK headers such as `cbplugin.h`, find platform-native plugin libraries, and parse sidecar `manifest.xml` files. It extracts the plugin name, title, version, description, license, and `SdkVersion` tuple.

The bridge understands the manifest shape used by the Code::Blocks source tree:

```xml
<CodeBlocks_plugin_manifest_file>
  <SdkVersion major="1" minor="36" release="0" />
  <Plugin name="Debugger">
    <Value title="Debugger" />
    <Value version="0.3" />
    <Value description="..." />
    <Value license="GPL" />
  </Plugin>
</CodeBlocks_plugin_manifest_file>
```

The native UI exposes **Extensions → Discover Code::Blocks SDK**, the command palette action **Discover Code::Blocks SDK**, and an Output-panel button. Discovery uses `CODEBLOCKS_ROOT` or `CODEBLOCKS_HOME` when configured and otherwise checks common platform installation roots. If automatic discovery fails, the command opens a directory chooser.

Codium::Blocks also imports a `.cbp` file located in the opened workspace. The importer reads the Code::Blocks `Project/Build/Target` structure, exposes each target in the scheme bar, creates a corresponding **Code::Blocks: Build target** task, preserves the project path and target name, and records the target compiler. The task invokes the documented Code::Blocks command-line shape `codeblocks --build --target="Target" project.cbp`, with the project filename in the final argument position. It does not assume that the executable is installed, so an unavailable command is reported by the normal task output and Problems pipeline.

## Why loading is not enabled yet

Code::Blocks plugins are not standalone C++ modules with a stable, host-independent ABI. A plugin registers through `PluginRegistrant<T>`, creates `cbPlugin` objects, and expects the Code::Blocks `Manager`, `PluginManager`, event system, log manager, editor manager, debugger manager, and SDK-specific global infrastructure to exist. The PluginManager also validates the embedded resource manifest and SDK version before loading a library.

Therefore, the bridge does **not** call `dlopen`, `LoadLibrary`, or `wxDynamicLibrary` on a discovered plugin. Treating a `.so`, `.dylib`, or `.dll` as directly loadable would risk executing plugin initializers against an incompatible host and could crash or corrupt the IDE. Every discovered plugin is currently reported as `manifest valid; SDK x.y.z; native loading disabled` when its sidecar manifest is valid.

## CMake option

The normal build remains independent of an installed Code::Blocks distribution. To expose an optional SDK include root to the native target:

```bash
cmake -S . -B build \
  -DCODIUM_BLOCKS_ENABLE_CODEBLOCKS_SDK=ON \
  -DCODIUM_BLOCKS_CODEBLOCKS_SDK_ROOT=/path/to/codeblocks
cmake --build build
```

This option currently adds `${root}/include` and `${root}/src/include` to the native target and defines `CODIUM_BLOCKS_CODEBLOCKS_SDK_ENABLED=1`. It does not link against or load the Code::Blocks core. That distinction keeps the default binary portable and makes the ABI boundary explicit.

## Next integration stage

The host-adapter contract is now versioned as **1.0** in `codium/codeblocks_host.hpp`. `CodeBlocksAdapterClient` launches an optional external adapter with a native argument vector, performs a JSON Lines handshake, validates the contract version, records capabilities, opens the imported project, and can route target builds through the adapter. Structured compiler diagnostics are translated into the native Problems model; the native UI falls back to the imported `codeblocks --build` task when no compatible adapter is running. See [`CODEBLOCKS_ADAPTER_PROTOCOL.md`](CODEBLOCKS_ADAPTER_PROTOCOL.md).

The next stage is a **real host adapter boundary**, not blind plugin loading. It must define which Code::Blocks managers are owned by Codium::Blocks, how `CodeBlocksEvent` messages are translated into the native problem/build/debug models, how plugin lifetime is isolated, and how SDK version compatibility is checked. Only after that adapter has a tested ABI contract should selected GPL-compatible plugins be loaded in-process or in a dedicated Code::Blocks compatibility process.
