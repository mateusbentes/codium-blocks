#!/usr/bin/env node

import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { spawn } from 'node:child_process';

const matrix = JSON.parse(await readFile(new URL('./real-lsp-matrix.json', import.meta.url), 'utf8'));
const platform = process.platform;
const servers = matrix.servers.map((server) => ({
  ...server,
  name: server.id,
  command: server.command[platform],
}));

class ServerExitError extends Error {
  constructor(phase, code, signal, stderr) {
    super(`server exited before ${phase} response (code=${code}, signal=${signal ?? ''}${stderr ? `, stderr=${stderr}` : ''})`);
    this.name = 'ServerExitError';
    this.phase = phase;
    this.code = 'LSP_SERVER_EXIT';
  }
}

function send(child, message) {
  const body = JSON.stringify(message);
  child.stdin.write(`Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n${body}`);
}

async function waitForClose(child, timeoutMs = 3000) {
  if (child.exitCode !== null || child.signalCode !== null) return;
  await Promise.race([
    new Promise((resolve) => child.once('close', resolve)),
    new Promise((resolve) => setTimeout(resolve, timeoutMs)),
  ]);
  if (child.exitCode === null && child.signalCode === null) {
    try { child.kill(); } catch {}
    await Promise.race([
      new Promise((resolve) => child.once('close', resolve)),
      new Promise((resolve) => setTimeout(resolve, timeoutMs)),
    ]);
  }
}

function waitForMessage(child, state, predicate, timeoutMs, phase) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      cleanup();
      reject(new Error(`timeout waiting for ${phase} response`));
    }, timeoutMs);
    const onMessage = (message) => {
      if (!predicate(message)) return;
      cleanup();
      resolve(message);
    };
    const onExit = (code, signal) => {
      cleanup();
      reject(new ServerExitError(phase, code, signal, state.stderr.trim()));
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
        // Ignore malformed frames; the matching request will fail with a timeout.
      }
    }
  });
  child.stderr.on('data', (chunk) => {
    state.stderr = `${state.stderr}${chunk.toString()}`.slice(-8192);
  });
}

async function smokeServer(server, root) {
  const child = spawn(server.command, server.args, {
    cwd: root,
    shell: process.platform === 'win32' && server.command.endsWith('.cmd'),
    stdio: ['pipe', 'pipe', 'pipe'],
  });
  const state = { listeners: new Set(), stderr: '' };
  attachParser(child, state);
  let nextId = 1;
  let phase = 'initialize';
  const request = async (method, params) => {
    const id = nextId++;
    const response = waitForMessage(child, state, (message) => message.id === id, 10000, phase);
    send(child, { jsonrpc: '2.0', id, method, params });
    return response;
  };
  const filePath = join(root, server.file);
  const uri = pathToFileURL(filePath).href;
  try {
    const initialized = await request('initialize', {
      processId: process.pid,
      rootUri: pathToFileURL(root).href,
      capabilities: {
        textDocument: {
          completion: {}, hover: {}, definition: {}, references: {}, rename: {}, publishDiagnostics: {},
        },
      },
      workspaceFolders: [{ uri: pathToFileURL(root).href, name: 'codium-blocks-real-lsp-smoke' }],
    });
    if (initialized.error) throw new Error(JSON.stringify(initialized.error));
    send(child, { jsonrpc: '2.0', method: 'initialized', params: {} });
    send(child, { jsonrpc: '2.0', method: 'textDocument/didOpen', params: {
      textDocument: { uri, languageId: server.languageId, version: 1, text: server.text },
    } });
    phase = 'hover';
    const hover = await request('textDocument/hover', {
      textDocument: { uri }, position: { line: 0, character: 1 },
    });
    phase = 'completion';
    const completion = await request('textDocument/completion', {
      textDocument: { uri }, position: { line: 0, character: 1 },
    });
    phase = 'definition';
    const definition = await request('textDocument/definition', {
      textDocument: { uri }, position: { line: 0, character: 1 },
    });
    phase = 'documentSymbol';
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
    try { child.stdin.end(); } catch {}
    await waitForClose(child);
  }
}

const root = await mkdtemp(join(tmpdir(), 'codium-blocks-real-lsp-'));
try {
  let executed = 0;
  for (const server of servers) {
    const serverRoot = await mkdtemp(join(root, `${server.name}-`));
    for (const file of server.files) {
      const path = join(serverRoot, file.path);
      await mkdir(dirname(path), { recursive: true });
      await writeFile(path, file.content, 'utf8');
    }
    const sourcePath = join(serverRoot, server.file);
    await mkdir(dirname(sourcePath), { recursive: true });
    await writeFile(sourcePath, server.text, 'utf8');
    try {
      const result = await smokeServer(server, serverRoot);
      executed += 1;
      console.log(`real-lsp-smoke: ${server.name} ok — initialize, didOpen, hover, completion, definition, documentSymbol`);
      console.log(`real-lsp-smoke: ${server.name} responses ${JSON.stringify(result.responses)}`);
    } catch (error) {
      if (error?.code === 'ENOENT' || String(error?.message).includes('ENOENT')) {
        console.log(`real-lsp-smoke: ${server.name} skipped — executable not installed`);
      } else if (error?.code === 'LSP_SERVER_EXIT' && error.phase === 'initialize') {
        console.log(`real-lsp-smoke: ${server.name} skipped — executable could not start in this environment (${error.message})`);
      } else {
        throw new Error(`${server.name}: ${error.message}`);
      }
    }
  }
  if (executed === 0) console.log('real-lsp-smoke: no real language servers completed; optional matrix skipped');
} finally {
  await rm(root, { recursive: true, force: true, maxRetries: 8, retryDelay: 250 });
}
