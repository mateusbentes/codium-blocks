# Extension Host Protocol

The prototype uses **JSON Lines** over `stdin`/`stdout`. Each line is an independent JSON object. The final transport may be replaced with named pipes, Unix domain sockets, or Windows named pipes without changing the logical contract.

## Host messages

```json
{"type":"ready","protocol":1,"runtime":"node","electron":false}
{"type":"event","event":"notification","level":"info","message":"..."}
{"type":"response","id":2,"ok":true}
```

## Broker messages

```json
{"id":1,"type":"hello"}
{"id":2,"type":"load","extensionPath":"/absolute/path"}
{"id":3,"type":"executeCommand","command":"hello.codium","args":[]}
{"id":4,"type":"listExtensions"}
{"id":5,"type":"shutdown"}
```

Protocol version `1` covers only the initial `vscode` subset: `commands`, `window`, `workspace`, `languages`, `extensions`, and `Uri`. The contract must be versioned before accepting third-party extensions.
