# Native Debugging

Codium::Blocks 0.9.0 communicates with a Debug Adapter Protocol implementation through standard `Content-Length` framing. The adapter path is selected at runtime, so installations can use `codelldb`, `OpenDebugAD7`, GDB's adapter, or another DAP-compatible process without bundling a debugger into the IDE.

The native UI can start an adapter, initialize it, launch a program, continue, pause, and disconnect. A source file's current editor line can be toggled as a breakpoint. The client sends a DAP `setBreakpoints` request with the source path and the complete line list for that file. Breakpoints are displayed in a native list and are kept as session state until the application exits.

The 0.9.0 client exposes requests for `threads`, `stackTrace`, `scopes`, `variables`, and `evaluate`. Responses are displayed in native Threads, Call stack, Variables/evaluate, and Debug console panels. When a stopped event supplies a thread identifier, Codium::Blocks automatically requests the thread list and stack trace. The implementation intentionally keeps the transport and basic presentation independent of any adapter-specific source mapping.

Language-server diagnostics are displayed as problems with line and character coordinates. Selecting a problem moves the editor caret to the reported location. A future increment will add richer diagnostic ranges, source mapping, clickable stack frames, watches, and adapter capability discovery.

The deterministic fake DAP process and transport test cover initialization, launch, threads, breakpoints, stack trace, scopes, variables, evaluate, continue, and clean shutdown. This validates the protocol layer without requiring a debugger installation.

A DAP adapter is an executable process and inherits the user's permissions. Workspace trust gates starting the adapter from a trusted workspace. The project does not claim that this process boundary is a security sandbox.
