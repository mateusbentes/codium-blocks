#!/usr/bin/env node

let buffer = Buffer.alloc(0);

function send(message) {
  const body = JSON.stringify(message);
  process.stdout.write(`Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n${body}`);
}

function handle(message) {
  if (message.method === 'initialize') {
    send({
      jsonrpc: '2.0',
      id: message.id,
      result: {
        capabilities: {
          hoverProvider: true,
          completionProvider: { triggerCharacters: ['.'] },
          textDocumentSync: 1,
        },
        serverInfo: { name: 'codium-blocks-fake-lsp', version: '0.1.0' },
      },
    });
    return;
  }

  if (message.method === 'textDocument/didOpen') {
    send({
      jsonrpc: '2.0',
      method: 'textDocument/publishDiagnostics',
      params: {
        uri: message.params.textDocument.uri,
        diagnostics: [{
          range: { start: { line: 0, character: 0 }, end: { line: 0, character: 5 } },
          severity: 2,
          message: 'Fake LSP diagnostic for integration testing',
          source: 'codium-blocks-fake-lsp',
        }],
      },
    });
    return;
  }

  if (message.method === 'textDocument/hover') {
    send({
      jsonrpc: '2.0',
      id: message.id,
      result: { contents: [{ language: 'text', value: 'Hover response from fake LSP' }] },
    });
    return;
  }

  if (message.method === 'textDocument/completion') {
    send({
      jsonrpc: '2.0',
      id: message.id,
      result: { isIncomplete: false, items: [{ label: 'codiumBlocksCompletion', detail: 'Fake LSP completion' }] },
    });
  }
}

process.stdin.on('data', (chunk) => {
  buffer = Buffer.concat([buffer, chunk]);
  while (true) {
    const separator = buffer.indexOf(Buffer.from('\r\n\r\n'));
    if (separator < 0) return;
    const headers = buffer.subarray(0, separator).toString('ascii');
    const length = Number(headers.match(/Content-Length:\s*(\d+)/i)?.[1] ?? 0);
    const bodyStart = separator + 4;
    if (!length || buffer.length < bodyStart + length) return;
    const body = buffer.subarray(bodyStart, bodyStart + length).toString('utf8');
    buffer = buffer.subarray(bodyStart + length);
    handle(JSON.parse(body));
  }
});
