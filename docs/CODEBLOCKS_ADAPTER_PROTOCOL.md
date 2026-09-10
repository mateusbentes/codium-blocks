# Code::Blocks Host Adapter Protocol

Codium::Blocks communicates with an optional Code::Blocks host adapter through **JSON Lines** over the adapter process standard input and standard output. The native application never loads a Code::Blocks library or plugin into its own address space through this protocol.

The compatible Phase A contract remains **1.0**. The `projectTarget` event and the `projectTargets` capability are additive within that contract. Clients must ignore events they do not use and must reject unsupported contract major versions.

## Handshake

The native client sends one handshake request after starting the adapter:

```json
{"type":"handshake","contractMajor":1,"contractMinor":0,"sdkMajor":0,"sdkMinor":0,"sdkRelease":0,"sdkRoot":"/path/to/codeblocks","projectFile":"/workspace/demo.cbp"}
```

An SDK tuple of `0.0.0` means that the client requests discovery and does not impose a specific SDK version. A non-zero tuple is an exact compatibility requirement. A production adapter must not silently substitute a different SDK ABI.

A compatible adapter responds with one `ready` message:

```json
{"type":"ready","contractMajor":1,"contractMinor":0,"sdkMajor":2,"sdkMinor":23,"sdkRelease":0,"sdkIdentity":"Code::Blocks SDK 2.23.0","capabilities":["sdkBootstrap","projectEvents","projectTargets","compilerPluginMatched"]}
```

The client accepts contract major `1` and a minor version from `0` through the currently implemented minor version. A future incompatible major version must be rejected before project or plugin operations begin. The `sdkIdentity` string is informational; the numeric SDK tuple is the compatibility value.

If initialization fails, the adapter emits a structured error and does not emit `ready`:

```json
{"type":"error","code":"bootstrapFailed","message":"Code::Blocks resources.zip could not be loaded."}
```

Possible Phase A error codes include `invalidArguments`, `sdkMismatch`, `bootstrapFailed`, `notReady`, and `projectLoadFailed`.

## Requests

The contract defines three request types:

| Type | Required fields | Phase A behavior |
|---|---|---|
| `openProject` | `projectFile` | Loads a `.cbp` through the real Code::Blocks `ProjectManager` and emits project events. |
| `build` | `projectFile`, `target`, `configuration` | Reports `unsupported` until the matched compiler-plugin lifecycle is implemented. It must not emit fake build success. |
| `shutdown` | none | Closes the loaded project, frees the Code::Blocks manager, and terminates the adapter. |

The adapter executable receives its runtime boundary separately from the JSON protocol. Its Phase A command line requires `--data-dir=DIR` for the Code::Blocks data directory and `--compiler-plugin=FILE` for the explicitly selected matching Compiler plugin. The adapter does not search or scan a plugin directory.

## Events

The adapter emits normalized events with `type: "event"`.

### Project opened

```json
{"type":"event","event":"projectOpened","projectPath":"/workspace/demo.cbp","message":"Project opened by Code::Blocks SDK"}
```

### Real target enumeration

The adapter emits one target event for every `cbProject` target returned by the SDK:

```json
{"type":"event","event":"projectTarget","projectPath":"/workspace/demo.cbp","target":"Debug","compilerId":"gcc","outputPath":"bin/debug/fixture","workingDirectory":"bin/debug","message":"Project target enumerated by Code::Blocks SDK"}
```

`target` is the Code::Blocks target title. `compilerId` is the target compiler identifier. `outputPath` and `workingDirectory` are the values reported by the SDK and may be relative to the project base path.

The existing normalized event names remain supported: `projectOpened`, `projectClosed`, `buildStarted`, `buildFinished`, `compilerDiagnostic`, `debugSessionStarted`, `debugSessionStopped`, and `pluginCommand`. Real compiler, debugger, and plugin-manager event translation is not claimed by Phase A.

Diagnostics use one-based line and column numbers so they can be displayed consistently with Code::Blocks output and converted to the native zero-based editor model.

## Process and security boundary

The adapter is an optional executable selected through `CODIUM_BLOCKS_CODEBLOCKS_ADAPTER` or a native file chooser. Its working directory is the active workspace, arguments are passed as a native argument vector, and the protocol does not invoke a POSIX shell. Workspace trust remains required before starting it or sending build requests.

Code::Blocks plugins are native C++ modules whose ABI depends on the host SDK, build flags, resources, and manager lifecycle. Phase A loads only the explicitly supplied, version-matched Compiler plugin in the dedicated adapter process. It does not load arbitrary discovered plugins, and it does not load any Code::Blocks plugin into `codium-blocks` itself. A process boundary limits the failure scope of the adapter but is not a trust mechanism for third-party native code.

The repository includes a deterministic fake adapter and a universal client smoke test. It also includes an optional Linux integration smoke test that uses the installed Code::Blocks SDK, official resources, a matched Compiler plugin, and `xvfb-run` to verify real `.cbp` target enumeration. The optional test is omitted when those runtime prerequisites are unavailable.

## References

[1]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/src/app.cpp "Code::Blocks application lifecycle"
[2]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/sdk/projectmanager.cpp "Code::Blocks ProjectManager implementation"
[3]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/sdk/pluginmanager.cpp "Code::Blocks PluginManager implementation"
[4]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/CODEBLOCKS_INTEGRATION.md "Codium::Blocks Code::Blocks integration policy"
