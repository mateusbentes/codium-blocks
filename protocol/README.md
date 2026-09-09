# Extension Host Protocol

The prototype uses **JSON Lines** over `stdin`/`stdout`. Each line is an independent JSON object. The final transport may be replaced with named pipes, Unix domain sockets, or Windows named pipes without changing the logical contract.

## Host messages

```json
{"type":"ready","protocol":2,"runtime":"node","electron":false,"capabilities":["configuration","documents","lsp-process-manager"]}
{"type":"event","event":"notification","level":"info","message":"..."}
{"type":"event","event":"contribution","kind":"command","command":"hello.codium","title":"Codium::Blocks: Hello"}
{"type":"event","event":"languageServerMessage","message":{"jsonrpc":"2.0"}}
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
{"id":7,"type":"stopLanguageServer"}
{"id":8,"type":"shutdown"}
```

Protocol version `2` adds persistent extension configuration, manifest contributions returned to the native UI, and an LSP process manager with standard `Content-Length` framing. The current native editor can open and save UTF-8 documents, while the LSP bridge is the foundation for diagnostics and completion in the next increment.
