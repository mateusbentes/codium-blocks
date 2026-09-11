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

```json
{"type":"ready","protocol":2,"runtime":"node","electron":false,"capabilities":["configuration","documents","lsp-process-manager"]}
{"type":"event","event":"notification","level":"info","message":"..."}
{"type":"event","event":"contribution","kind":"command","command":"hello.codium","title":"Codium::Blocks: Hello"}
{"type":"event","event":"languageServerMessage","message":{"jsonrpc":"2.0"}}
{"type":"event","event":"languageServerResult","method":"textDocument/hover","result":{}}
{"type":"event","event":"diagnostics","uri":"file:///workspace/main.cpp","diagnostics":[]}
{"type":"response","id":2,"ok":true}
```

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

Protocol version 2 adds persistent extension configuration, manifest contributions returned to the native UI, workspace document notifications, Tree View contributions, and an LSP process manager with standard `Content-Length` framing. The native client sends `initialize`, `initialized`, `textDocument/didOpen`, and `textDocument/didChange`, and can request hover, completion, semantic tokens, definitions, references, rename, and code actions. LSP responses and `textDocument/publishDiagnostics` notifications are forwarded to the native event loop as raw messages plus normalized events.

## Compatibility maintenance

Any wire-contract change must update this document, `tests/host-smoke.mjs`, the integration matrix, and the host implementation together. Product names, JSON keys, protocol identifiers, and LSP/DAP method names are protocol data and must not be localized.
