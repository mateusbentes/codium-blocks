# Native Debugging

Codium::Blocks communicates with a Debug Adapter Protocol implementation through standard `Content-Length` framing. The adapter path is selected at runtime, so installations can use GDB's DAP interpreter, LLDB-DAP, OpenDebugAD7, CodeLLDB, or another DAP-compatible process without bundling a debugger into the IDE.

The native UI can start an adapter, initialize it, launch a program, continue, pause, and disconnect. A source file's current editor line can be toggled as a breakpoint. The client sends a DAP `setBreakpoints` request with the source path and the complete breakpoint specification for that file. Each specification may include a conditional expression, hit-count expression, or log message. Source breakpoints are displayed in the native list and gutter, persist per workspace in escaped UTF-8 TSV, and retain pending, verified, or rejected adapter state during a session.

When the adapter advertises `supportsFunctionBreakpoints`, the Debug panel can send `setFunctionBreakpoints`. When it advertises `supportsDataBreakpoints`, the panel can send `setDataBreakpoints` with a DAP `dataId` and access type. These advanced requests are session-scoped in the current increment. The UI does not pretend that an adapter supports a capability that was not present in its initialize response.

The client exposes requests for `threads`, `stackTrace`, `scopes`, `variables`, `evaluate`, and `configurationDone`. The initialize response is inspected for common adapter capabilities and shown in the Adapter capabilities panel. Responses are displayed in native Threads, Call stack, Variables/evaluate, and Debug console panels. When a stopped event supplies a thread identifier, Codium::Blocks automatically requests the thread list and stack trace.

Source mappings can be registered from a remote source root to a local source root. The mapping is sent as DAP `sourceFileMap` during launch and is applied when a stack frame path is opened. When a stopped event produces a stack trace, the first active frame is synchronized automatically with the mapped editor document, tab, caret, and Debug status. Selecting another call-stack frame repeats that synchronization and refreshes its scopes. Watches are stored outside the source tree using the platform data directory and are evaluated through DAP when a debug frame is available.

`tests/real-dap-matrix.json` is the versioned adapter contract for GDB, LLDB-DAP, and OpenDebugAD7. `tests/real-dap-smoke.mjs` probes the standard initialize request for adapters installed on the current system. A missing executable is reported as a bounded environment skip. An executable that starts but rejects or fails to complete initialize is a test failure. The generic matrix is separate from the optional matched Code::Blocks DebuggerGDB provider workflow.

The deterministic fake DAP process and transport test cover initialization, configuration, launch, threads, source breakpoints, conditional fields, logpoints, function breakpoints, data breakpoints, stack trace, scopes, variables, evaluate, continue, and clean shutdown. This validates the protocol layer without requiring a debugger installation.

A DAP adapter is an executable process and inherits the user's permissions. Workspace trust gates starting the adapter from a trusted workspace. The project does not claim that this process boundary is a security sandbox.

## Capability boundary

| Capability | Native behavior | Validation |
|---|---|---|
| Source breakpoints | Persistent per workspace, rendered in the gutter, and correlated to the source-specific response. | Fake DAP transport and full native smoke suite. |
| Conditional, hit-count, and logpoint fields | Serialized only when configured and resent as one complete source request. | Fake DAP payload assertions. |
| Function breakpoints | Sent through `setFunctionBreakpoints` after capability discovery. | Fake DAP payload assertions; real runtime matrix probe is adapter-dependent. |
| Data breakpoints | Sent through `setDataBreakpoints` after capability discovery. | Fake DAP payload assertions; real runtime matrix probe is adapter-dependent. |
| Private Code::Blocks debugger data | Available only through the separately compiled, identity-matched provider boundary. | Dedicated optional Linux workflow; never loaded by the main process. |
