# Extension Host Protocol

The prototype uses **JSON Lines** over `stdin`/`stdout`. Each line is an independent JSON object. The final transport may be replaced with named pipes, Unix domain sockets, or Windows named pipes without changing the logical contract.

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

Protocol version `2` adds persistent extension configuration, manifest contributions returned to the native UI, and an LSP process manager with standard `Content-Length` framing. The native client now sends `initialize`, `initialized`, `textDocument/didOpen`, and `textDocument/didChange`, and can request hover and completion. LSP responses and `textDocument/publishDiagnostics` notifications are forwarded to the native event loop as raw messages plus normalized `languageServerResult` and `diagnostics` events. A fake LSP server exercises this contract in the automated test suite.
