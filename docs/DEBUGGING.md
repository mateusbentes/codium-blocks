# Native Debugging

Codium::Blocks 1.0.1 communicates with a Debug Adapter Protocol implementation through standard `Content-Length` framing. The adapter path is selected at runtime, so installations can use `codelldb`, `OpenDebugAD7`, GDB's adapter, or another DAP-compatible process without bundling a debugger into the IDE.

The native UI can start an adapter, initialize it, launch a program, continue, pause, and disconnect. A source file's current editor line can be toggled as a breakpoint. The client sends a DAP `setBreakpoints` request with the source path and the complete breakpoint specification for that file. Each specification may include a conditional expression, hit condition, or log message. Breakpoints are displayed in the native list and gutter, persist per workspace in the platform data directory, and retain pending, verified, or rejected adapter state during a session.

The 0.9.0 client exposes requests for `threads`, `stackTrace`, `scopes`, `variables`, `evaluate`, and `configurationDone`. The initialize response is inspected for common adapter capabilities and shown in the Adapter capabilities panel. Responses are displayed in native Threads, Call stack, Variables/evaluate, and Debug console panels. When a stopped event supplies a thread identifier, Codium::Blocks automatically requests the thread list and stack trace.

Source mappings can be registered from a remote source root to a local source root. The mapping is sent as DAP `sourceFileMap` during launch and is applied when a stack frame path is opened. When a stopped event produces a stack trace, the first active frame is synchronized automatically with the mapped editor document, tab, caret, and Debug status. Selecting another call-stack frame repeats that synchronization and refreshes its scopes. Watches are stored outside the source tree using the platform data directory and are evaluated through DAP when a debug frame is available.

Language-server diagnostics are displayed as problems with line and character coordinates. Selecting a problem moves the editor caret to the reported location. Adapter-specific source mapping and richer stack-frame metadata are supported where the adapter supplies them. More advanced debugger capabilities remain explicitly dependent on the DAP adapter's advertised support.

The deterministic fake DAP process and transport test cover initialization, configuration, launch, threads, breakpoints, stack trace, scopes, variables, evaluate, continue, and clean shutdown. This validates the protocol layer without requiring a debugger installation.

A DAP adapter is an executable process and inherits the user's permissions. Workspace trust gates starting the adapter from a trusted workspace. The project does not claim that this process boundary is a security sandbox.
