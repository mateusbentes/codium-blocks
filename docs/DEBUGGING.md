# Terminal and Debugging

Codium::Blocks 0.7.0 adds two independent native transports. `TerminalSession` launches the platform shell with an explicit workspace directory, exposes stdin for interactive input, and streams stdout/stderr into the native output panel. On Windows it uses `cmd.exe`; on macOS and Linux it uses the system POSIX shell. Terminal support is optional and is not loaded during application startup.

`DapClient` communicates with a Debug Adapter Protocol implementation through the standard `Content-Length` framing used by DAP. The native UI can start an adapter executable, send `initialize`, `launch`, `continue`, `pause`, and `disconnect` requests, and display adapter responses in the output panel. The adapter path is selected at runtime, so installations can use `codelldb`, `OpenDebugAD7`, or another DAP-compatible adapter without bundling one into the IDE.

The 0.7.0 transport test starts deterministic fake terminal and fake DAP processes. It verifies interactive stdin/stdout, DAP framing, initialization events, launch responses, thread enumeration, and continue responses without requiring a debugger installation. This validates the transport independently from adapter-specific behavior.

The current UI is a transport foundation rather than a complete debugger. Breakpoint markers, source mapping, stack frames, variables, watches, and clickable stop locations are planned for the next debugging increment.
