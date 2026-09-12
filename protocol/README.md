# Extension Host Protocol

The native application communicates with the optional Node.js Extension Host through **JSON Lines** over `stdin`/`stdout`. Each line is an independent JSON object. The transport may later use named pipes, Unix-domain sockets, or Windows named pipes without changing the logical contract.

## Version negotiation

Protocol version **2** is the current canonical contract. The host advertises it in the initial `ready` event and responds with it to the broker `hello` request. A client must reject a host that reports an unsupported protocol version instead of silently assuming that message shapes are compatible.

| Version | Status | Scope |
|---|---|---|
| 1 | Historical prototype | Initial commands, window, workspace, languages, extensions, and `Uri` subset. It is retained only as historical documentation. |
| 2 | Current | Versioned configuration, extension contributions, workspace document events, Tree View contributions, LSP process management, and the current native smoke-test contract. |

Protocol 2 is an additive evolution of the prototype, but it is not a promise of complete VS Code API compatibility. Unsupported requests must produce an explicit failure response.

## Host messages

The initial event is representative of the current host contract. Capability names are protocol data and are not localized.

```json
{"type":"ready","protocol":2,"runtime":"node","electron":false,"api":["commands","window","workspace","languages","extensions","Uri","TreeItem"],"capabilities":["configuration","documents","workspace-events","tree-views","lsp-process-manager"]}
{"type":"event","event":"notification","level":"info","message":"..."}
{"type":"event","event":"contribution","kind":"command","command":"hello.codium","title":"Codium::Blocks: Hello"}
{"type":"event","event":"languageServerMessage","message":{"jsonrpc":"2.0"}}
{"type":"event","event":"languageServerResult","method":"textDocument/hover","result":{}}
{"type":"event","event":"diagnostics","uri":"file:///workspace/main.cpp","diagnostics":[]}
{"type":"event","event":"workspaceDocument","action":"open","uri":"file:///workspace/main.cpp","version":1}
{"type":"event","event":"treeView","extension":"codium-blocks.hello-codium","viewId":"hello.codium.views","items":[]}
{"type":"response","id":2,"ok":true}
```

The `ready` event is emitted before requests are accepted. The `hello` response repeats the negotiated protocol version and reports `electron:false`; it is the request/response handshake that native clients should use before sending extension or language-server commands.

## Broker messages

```json
{"id":1,"type":"hello"}
{"id":2,"type":"load","extensionPath":"/absolute/path"}
{"id":3,"type":"executeCommand","command":"hello.codium","args":[]}
{"id":4,"type":"listExtensions"}
{"id":5,"type":"startLanguageServer","command":"clangd","args":[],"cwd":"/workspace"}
{"id":6,"type":"languageServerRequest","message":{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}}
{"id":7,"type":"languageServerNotification","message":{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{}}}
{"id":8,"type":"stopLanguageServer"}
{"id":9,"type":"shutdown"}
```

Protocol version 2 adds persistent extension configuration, manifest contributions returned to the native UI, workspace document notifications, Tree View contributions, and an LSP process manager with standard `Content-Length` framing aligned with the Language Server Protocol specification [1]. The native client sends `initialize`, `initialized`, `textDocument/didOpen`, and `textDocument/didChange`, and can request hover, completion, semantic tokens, definitions, references, rename, and code actions. Native debug-adapter communication follows the corresponding Debug Adapter Protocol framing and lifecycle conventions [2]. LSP responses and `textDocument/publishDiagnostics` notifications are forwarded to the native event loop as raw messages plus normalized events.

## Security boundary

The Extension Host is a separate Node.js process, not a sandbox. Extensions, language servers, tasks, and debug adapters can still run with the user's operating-system permissions. The host accepts only extension paths inside its configured runtime extension roots by default; this path boundary is containment validation, not a substitute for signatures, workspace trust, operating-system sandboxing, or a publisher trust store. See [`docs/EXTENSIONS_SECURITY.md`](../docs/EXTENSIONS_SECURITY.md).

## Compatibility maintenance

Any wire-contract change must update this document, `tests/host-smoke.mjs`, the integration matrix, and the host implementation together. Product names, JSON keys, protocol identifiers, and LSP/DAP method names are protocol data and must not be localized.

## References

1. [Language Server Protocol specification](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/)
2. [Debug Adapter Protocol specification](https://microsoft.github.io/debug-adapter-protocol/specification)

[1]: https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/ "Language Server Protocol specification"
[2]: https://microsoft.github.io/debug-adapter-protocol/specification "Debug Adapter Protocol specification"
