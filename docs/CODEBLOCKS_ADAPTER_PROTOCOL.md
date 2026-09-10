# Code::Blocks Host Adapter Protocol

Codium::Blocks communicates with an optional Code::Blocks host adapter through **JSON Lines** over the adapter process standard input and standard output. The native application never loads a Code::Blocks library or plugin into its own address space through this protocol.

The compatible adapter contract is **1.2**. Versions `1.0` and `1.1` remain compatible because the debugger snapshot request and event are additive. Clients must ignore fields and events they do not use and must reject unsupported contract major versions.

## Handshake

The native client sends one handshake request after starting the adapter:

```json
{"type":"handshake","contractMajor":1,"contractMinor":1,"sdkMajor":0,"sdkMinor":0,"sdkRelease":0,"sdkRoot":"/path/to/codeblocks","projectFile":"/workspace/demo.cbp"}
```

An SDK tuple of `0.0.0` means that the client requests discovery and does not impose a specific SDK version. A non-zero tuple is an exact compatibility requirement. A production adapter must not silently substitute a different SDK ABI.

A compatible adapter responds with one `ready` message:

```json
{"type":"ready","contractMajor":1,"contractMinor":2,"sdkMajor":2,"sdkMinor":23,"sdkRelease":0,"sdkIdentity":"Code::Blocks SDK 2.23.0","capabilities":["sdkBootstrap","sdkEventSink","projectEvents","projectTargets","compilerEvents","compilerBuild","compilerOutput","compilerPluginMatched","debuggerPluginMatched","debuggerEvents","debuggerControl","debuggerSnapshot","debuggerPublicState","debuggerDataUnavailable"]}
```

The client accepts contract major `1` and a minor version from `0` through the currently implemented minor version. A future incompatible major version must be rejected before project or plugin operations begin. The `sdkIdentity` string is informational; the numeric SDK tuple is the compatibility value.

If initialization fails, the adapter emits a structured error and does not emit `ready`:

```json
{"type":"error","code":"bootstrapFailed","message":"Code::Blocks resources.zip could not be loaded."}
```

Possible error codes include `invalidArguments`, `sdkMismatch`, `bootstrapFailed`, `notReady`, `projectLoadFailed`, `buildStartFailed`, `debugStartFailed`, `debugControlFailed`, `debugSnapshotFailed`, and `debugDataUnavailable`.

## Requests

The contract defines the following request types:

| Type | Required fields | Phase C behavior |
|---|---|---|
| `openProject` | `projectFile` | Loads a `.cbp` through the real Code::Blocks `ProjectManager` and emits project events. |
| `build` | `projectFile`, `target`, `configuration` | Invokes the matched Code::Blocks Compiler plugin for the selected target, then emits asynchronous compiler lifecycle/output events. It never emits fake build success. |
| `debug` | `projectFile`, `target`, `breakOnEntry` | Starts the matched Code::Blocks Debugger plugin for the selected project target. It requires the debugger capabilities in the `ready` response. |
| `continueDebug` | none | Continues the active Code::Blocks debug session. |
| `pauseDebug` | none | Breaks the active debuggee. |
| `stopDebug` | none | Stops the active Code::Blocks debug session. |
| `requestDebugSnapshot` | `dataKind` (`state`, `frames`, `threads`, `breakpoints`, `watches`, or `variables`) | Requests a value-owned debugger data snapshot. Phase E.1 implements `state`; private model kinds return `debugDataUnavailable`. |
| `shutdown` | none | Closes the loaded project, frees the Code::Blocks manager, and terminates the adapter. |

The adapter executable receives its runtime boundary separately from the JSON protocol. Its command line requires `--data-dir=DIR` for the Code::Blocks data directory and `--compiler-plugin=FILE` for the explicitly selected matching Compiler plugin. `--debugger-plugin=FILE` is optional and enables the matched Debugger capability. When present, the adapter requires the Compiler and Debugger files to come from the same plugin directory, stages only those two files, and scans that private staging directory. The adapter never scans the user's complete plugin directory.

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

### Official SDK project events

After bootstrap, the adapter registers typed event sinks with the Code::Blocks `Manager`. The following official SDK events are normalized without exposing SDK pointers across the process boundary:

| Code::Blocks event | Normalized event | Important fields |
|---|---|---|
| `cbEVT_PROJECT_OPEN` | `projectOpened` | `projectPath`, `message` |
| `cbEVT_PROJECT_CLOSE` | `projectClosed` | `projectPath`, `message` |
| `cbEVT_PROJECT_ACTIVATE` | `projectActivated` | `projectPath`, `target` |
| `cbEVT_PROJECT_SAVE` | `projectSaved` | `projectPath` |
| `cbEVT_PROJECT_TARGETS_MODIFIED` | `projectTargetsChanged` | `projectPath` |
| `cbEVT_PROJECT_FILE_ADDED` | `projectFileAdded` | `projectPath`, `filePath` |
| `cbEVT_PROJECT_FILE_REMOVED` | `projectFileRemoved` | `projectPath`, `filePath` |
| `cbEVT_PROJECT_FILE_CHANGED` | `projectFileChanged` | `projectPath`, `filePath` |
| `cbEVT_PROJECT_FILE_RENAMED` | `projectFileRenamed` | `projectPath`, `oldFilePath`, `filePath` |

For example:

```json
{"type":"event","event":"projectFileRenamed","projectPath":"/workspace/demo.cbp","filePath":"src/new.cpp","oldFilePath":"src/old.cpp","message":"Code::Blocks emitted cbEVT_PROJECT_FILE_RENAMED"}
```

### Official compiler lifecycle events

The sink also normalizes the official compiler lifecycle events emitted by the matched Compiler plugin:

| Code::Blocks event | Normalized event | Important fields |
|---|---|---|
| `cbEVT_COMPILER_STARTED` | `buildStarted` | `projectPath`, `target`, `plugin` |
| `cbEVT_COMPILER_FINISHED` | `buildFinished` | `projectPath`, `target`, `plugin`, `exitCode`, `isError` |
| `cbEVT_PIPEDPROCESS_STDOUT` | `compilerOutput` | `projectPath`, `target`, `plugin`, `message`, `payload` |
| `cbEVT_PIPEDPROCESS_STDERR` | `compilerOutput` | `projectPath`, `target`, `plugin`, `message`, `payload`, `isError` |

`cbEVT_COMPILER_FINISHED` carries its exit status in the SDK event integer field; the adapter preserves it as `exitCode` and sets `isError` for non-zero values. Piped compiler lines are forwarded without SDK pointers. stderr lines are marked `isError` because the SDK does not expose a richer severity classification at this boundary; the native Problems parser can still distinguish GCC/Clang warning text from errors.

The existing normalized event names remain supported: `projectOpened`, `projectClosed`, `projectActivated`, `projectSaved`, `projectTargetsChanged`, `projectFileAdded`, `projectFileRemoved`, `projectFileChanged`, `projectFileRenamed`, `buildStarted`, `buildFinished`, `compilerOutput`, `compilerDiagnostic`, `debugSessionStarted`, `debugSessionStopped`, `debugSessionPaused`, `debugSessionContinued`, `debugSessionCursorChanged`, `debugSessionUpdated`, `debugSnapshot`, and `pluginCommand`.

Diagnostics use one-based line and column numbers so they can be displayed consistently with Code::Blocks output and converted to the native zero-based editor model.

### Official debugger lifecycle events

When the optional Debugger plugin is enabled, the adapter registers the official debugger event types and converts them to value-owned protocol events:

| Code::Blocks event | Normalized event | Important fields |
|---|---|---|
| `cbEVT_DEBUGGER_STARTED` | `debugSessionStarted` | `projectPath`, `target`, `plugin` |
| `cbEVT_DEBUGGER_PAUSED` | `debugSessionPaused` | `projectPath`, `target`, `plugin` |
| `cbEVT_DEBUGGER_CONTINUED` | `debugSessionContinued` | `projectPath`, `target`, `plugin` |
| `cbEVT_DEBUGGER_FINISHED` | `debugSessionStopped` | `projectPath`, `target`, `plugin`, `exitCode`, `isError` |
| `cbEVT_DEBUGGER_CURSOR_CHANGED` | `debugSessionCursorChanged` | `projectPath`, `target`, `plugin`, `line`, `column` when supplied by the SDK |
| `cbEVT_DEBUGGER_UPDATED` | `debugSessionUpdated` | `projectPath`, `target`, `plugin`, `exitCode` containing the SDK update kind |

The Phase D adapter supplies no-op debugger windows and menu handlers inside the adapter process. This lets a matched Debugger plugin execute its real GDB session without requiring Code::Blocks GUI objects in the main Codium::Blocks process. Phase E.1 adds a `debugSnapshot` event for public, value-owned debugger state.

The `state` snapshot contains running, stopped, busy, exit-code, active-frame, current-source-location, breakpoint-count, and `SupportsFeature` flags. The adapter does not dereference `cbStackFrame`, `cbThread`, `cbBreakpoint`, or `cbWatch` objects in the generic build because the installed public SDK only forward-declares those model types. Requests for `frames`, `threads`, `breakpoints`, `watches`, or `variables` therefore return `debugDataUnavailable` and keep the session alive. Full model transport requires a separately matched DebuggerGDB private-provider build with an exact source, ABI, and compiler identity.

The event uses value-owned fields only:

```json
{"type":"event","event":"debugSnapshot","plugin":"Debugger","dataKind":"state","snapshotJson":"{\"stopped\":true,\"activeFrame\":0,\"privateDataAvailable\":false}","message":"Public Code::Blocks debugger state snapshot"}
```

An unsupported request is explicit and non-fatal:

```json
{"type":"error","code":"debugDataUnavailable","dataKind":"frames","capability":"debuggerDataUnavailable","message":"The matched Code::Blocks SDK does not expose value-owned frames data through its public ABI."}
```

## Process and security boundary

The adapter is an optional executable selected through `CODIUM_BLOCKS_CODEBLOCKS_ADAPTER` or a native file chooser. Its working directory is the active workspace, arguments are passed as a native argument vector, and the protocol does not invoke a POSIX shell. Workspace trust remains required before starting it or sending build requests.

Code::Blocks plugins are native C++ modules whose ABI depends on the host SDK, build flags, resources, and manager lifecycle. Phase D loads only explicitly supplied, version-matched Compiler and Debugger plugins from one matched installation in the dedicated adapter process. It does not load arbitrary discovered plugins, and it does not load any Code::Blocks plugin into `codium-blocks` itself. A process boundary limits the failure scope of the adapter but is not a trust mechanism for third-party native code. Native debugger loading is refused for an untrusted workspace.

The repository includes a deterministic fake adapter and a universal client smoke test. It also includes an optional Linux integration smoke test that uses the installed Code::Blocks SDK, official resources, matched Compiler and Debugger plugins, and `xvfb-run` to verify real `.cbp` target enumeration, compiler lifecycle events, compiler output, a produced executable, debugger launch, debugger events, the public state snapshot, and graceful rejection of private model data. The optional test is omitted when those runtime prerequisites are unavailable.

## References

[1]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/src/app.cpp "Code::Blocks application lifecycle"
[2]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/sdk/projectmanager.cpp "Code::Blocks ProjectManager implementation"
[3]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/sdk/pluginmanager.cpp "Code::Blocks PluginManager implementation"
[4]: https://github.com/mateusbentes/codium-blocks/blob/main/docs/CODEBLOCKS_INTEGRATION.md "Codium::Blocks Code::Blocks integration policy"
[5]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/include/cbplugin.h "Code::Blocks public debugger plugin interface"
[6]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/include/sdk_events.h "Code::Blocks official debugger events"
[7]: https://svn.code.sf.net/p/codeblocks/code/trunk/src/plugins/debuggergdb/debugger_defs.h "Code::Blocks DebuggerGDB private data definitions"
