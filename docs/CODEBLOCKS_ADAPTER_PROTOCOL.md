# Code::Blocks Host Adapter Protocol

Codium::Blocks communicates with an optional Code::Blocks host adapter through **JSON Lines** over the adapter process standard input and standard output. The native application never loads a Code::Blocks plugin library into its own address space through this protocol.

## Handshake

The native client sends one handshake request after starting the adapter:

```json
{"type":"handshake","contractMajor":1,"contractMinor":0,"sdkMajor":1,"sdkMinor":36,"sdkRelease":0,"sdkRoot":"/path/to/codeblocks","projectFile":"/workspace/demo.cbp"}
```

A compatible adapter responds with one `ready` message:

```json
{"type":"ready","contractMajor":1,"contractMinor":0,"sdkMajor":1,"sdkMinor":36,"sdkRelease":0,"capabilities":["projectEvents","buildEvents","compilerDiagnostics","debugEvents"]}
```

The client accepts only contract major `1` and a minor version from `0` through the currently implemented minor version. A future incompatible major version must be rejected before project or plugin operations begin.

## Requests

The first implementation defines three requests:

| Type | Required fields | Purpose |
|---|---|---|
| `openProject` | `projectFile` | Ask the adapter to load a `.cbp` project through the Code::Blocks host. |
| `build` | `projectFile`, `target`, `configuration` | Build a target and report structured output/events. |
| `shutdown` | none | Ask the adapter to stop before the native process terminates it. |

## Events

The adapter emits normalized events with `type: "event"`:

```json
{"type":"event","event":"compilerDiagnostic","projectPath":"/workspace/demo.cbp","target":"app","filePath":"src/main.cpp","line":12,"column":4,"message":"unused variable","isError":false}
```

Supported event names are `projectOpened`, `projectClosed`, `buildStarted`, `buildFinished`, `compilerDiagnostic`, `debugSessionStarted`, `debugSessionStopped`, and `pluginCommand`. Build completion carries an integer `exitCode`; diagnostics use one-based line and column numbers so they can be displayed consistently with Code::Blocks output and converted to the native zero-based editor model.

## Process and security boundary

The adapter is an optional executable selected through `CODIUM_BLOCKS_CODEBLOCKS_ADAPTER` or a native file chooser. Its working directory is the active workspace, arguments are passed as a native argument vector, and the protocol does not invoke a POSIX shell. The adapter may still access the user's normal process permissions; therefore workspace trust remains required before starting it or sending build requests.

The current repository includes a deterministic fake adapter and an integration smoke test. The fake validates transport and event normalization but is not a Code::Blocks implementation. A production adapter must host the matching Code::Blocks SDK, validate plugin manifests and SDK versions, and define a documented policy for plugin capabilities before loading any third-party native module.
