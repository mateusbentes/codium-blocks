#!/usr/bin/env node

import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { spawn } from 'node:child_process';

const servers = [
  { name: 'clangd', command: 'clangd', args: ['--log=error'], file: 'main.cpp', languageId: 'cpp', text: 'int main() { return 0; }\n' },
  { name: 'rust-analyzer', command: 'rust-analyzer', args: [], file: 'main.rs', languageId: 'rust', text: 'fn main() { }\n' },
  { name: 'gopls', command: 'gopls', args: ['serve'], file: 'main.go', languageId: 'go', text: 'package main\nfunc main() {}\n' },
  { name: 'pyright', command: 'pyright-langserver', args: ['--stdio'], file: 'main.py', languageId: 'python', text: 'def main():\n    return 0\n' },
];

function send(child, message) {
  const body = JSON.stringify(message);
  child.stdin.write(`Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n${body}`);
}

function waitForMessage(child, state, predicate, timeoutMs) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      cleanup();
      reject(new Error('timeout waiting for LSP response'));
    }, timeoutMs);
    const onMessage = (message) => {
      if (!predicate(message)) return;
      cleanup();
      resolve(message);
    };
    const onExit = (code, signal) => {
      cleanup();
      reject(new Error(`server exited before response (code=${code}, signal=${signal ?? ''})`));
    };
    const onError = (error) => {
      cleanup();
      reject(error);
    };
    const cleanup = () => {
      clearTimeout(timeout);
      state.listeners.delete(onMessage);
      child.off('exit', onExit);
      child.off('error', onError);
    };
    state.listeners.add(onMessage);
    child.once('exit', onExit);
    child.once('error', onError);
  });
}

function attachParser(child, state) {
  let buffer = Buffer.alloc(0);
  child.stdout.on('data', (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    while (true) {
      const separator = buffer.indexOf(Buffer.from('\r\n\r\n'));
      if (separator < 0) return;
      const header = buffer.subarray(0, separator).toString('ascii');
      const match = header.match(/Content-Length:\s*(\d+)/i);
      if (!match) {
        buffer = buffer.subarray(separator + 4);
        continue;
      }
      const bodyStart = separator + 4;
      const length = Number(match[1]);
      if (buffer.length < bodyStart + length) return;
      const body = buffer.subarray(bodyStart, bodyStart + length).toString('utf8');
      buffer = buffer.subarray(bodyStart + length);
      try {
        const message = JSON.parse(body);
        for (const listener of [...state.listeners]) listener(message);
      } catch {
        // A malformed server frame is ignored here; the awaited request will time out.
      }
    }
  });
}

async function smokeServer(server, root) {
  const child = spawn(server.command, server.args, { cwd: root, stdio: ['pipe', 'pipe', 'pipe'] });
  const state = { listeners: new Set() };
  attachParser(child, state);
  let nextId = 1;
  const request = async (method, params) => {
    const id = nextId++;
    const response = waitForMessage(child, state, (message) => message.id === id, 10000);
    send(child, { jsonrpc: '2.0', id, method, params });
    return response;
  };
  const filePath = join(root, server.file);
  const uri = pathToFileURL(filePath).href;
  try {
    const initialized = await request('initialize', {
      processId: process.pid,
      rootUri: pathToFileURL(root).href,
      capabilities: { textDocument: { completion: {}, hover: {}, definition: {}, references: {}, rename: {}, publishDiagnostics: {} } },
      workspaceFolders: [{ uri: pathToFileURL(root).href, name: 'codium-blocks-real-lsp-smoke' }],
    });
    if (initialized.error) throw new Error(JSON.stringify(initialized.error));
    send(child, { jsonrpc: '2.0', method: 'initialized', params: {} });
    send(child, { jsonrpc: '2.0', method: 'textDocument/didOpen', params: {
      textDocument: { uri, languageId: server.languageId, version: 1, text: server.text },
    } });
    const hover = await request('textDocument/hover', {
      textDocument: { uri }, position: { line: 0, character: 1 },
    });
    const completion = await request('textDocument/completion', {
      textDocument: { uri }, position: { line: 0, character: 1 },
    });
    const definition = await request('textDocument/definition', {
      textDocument: { uri }, position: { line: 0, character: 1 },
    });
    const symbols = await request('textDocument/documentSymbol', { textDocument: { uri } });
    return {
      initialized: true,
      providers: initialized.result?.capabilities ?? {},
      responses: {
        hover: !hover.error,
        completion: !completion.error,
        definition: !definition.error,
        documentSymbol: !symbols.error,
      },
    };
  } finally {
    try { send(child, { jsonrpc: '2.0', id: 999, method: 'shutdown', params: null }); } catch {}
    child.kill();
  }
}

const root = await mkdtemp(join(tmpdir(), 'codium-blocks-real-lsp-'));
try {
  for (const server of servers) await writeFile(join(root, server.file), server.text, 'utf8');
  let executed = 0;
  for (const server of servers) {
    try {
      const result = await smokeServer(server, root);
      executed += 1;
      console.log(`real-lsp-smoke: ${server.name} ok — initialize, didOpen, hover, completion, definition, documentSymbol`);
      console.log(`real-lsp-smoke: ${server.name} responses ${JSON.stringify(result.responses)}`);
    } catch (error) {
      if (error?.code === 'ENOENT' || String(error?.message).includes('ENOENT')) {
        console.log(`real-lsp-smoke: ${server.name} skipped — executable not installed`);
      } else {
        throw new Error(`${server.name}: ${error.message}`);
      }
    }
  }
  if (executed === 0) console.log('real-lsp-smoke: no real language servers installed; optional matrix skipped');
} finally {
  await rm(root, { recursive: true, force: true });
}
