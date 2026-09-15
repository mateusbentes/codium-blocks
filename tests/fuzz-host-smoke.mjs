#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

import assert from 'node:assert/strict';
import { createInterface } from 'node:readline';
import { once } from 'node:events';
import { mkdir, rm, writeFile } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(process.env.CODIUM_BLOCKS_ROOT ?? fileURLToPath(new URL('../', import.meta.url)));
const hostScript = join(root, 'extension-host', 'src', 'host.mjs');
const tempRoot = join(process.env.TMPDIR ?? tmpdir(), 'codium-blocks-host-fuzz');
const iterations = Math.max(16, Math.min(256, Number.parseInt(process.env.CODIUM_BLOCKS_FUZZ_ITERATIONS ?? '64', 10) || 64));
const seed = 0xBADC0DE;

function next(state) {
  state.value = (Math.imul(state.value, 1664525) + 1013904223) >>> 0;
  return state.value;
}

function dapFrame(body) {
  return `Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n${body}`;
}

function lspPayload(state, iteration) {
  switch (iteration % 7) {
    case 0:
      return dapFrame(JSON.stringify({ jsonrpc: '2.0', id: iteration + 1, result: { ok: true } }));
    case 1:
      return 'content-length: 2\r\nX-Fuzz: yes\r\n\r\n{}';
    case 2:
      return 'Content-Length: invalid\r\n\r\n{}';
    case 3:
      return 'Content-Length: 999999999999999999999\r\n\r\n';
    case 4:
      return `${'A'.repeat(65540)}\r\n\r\n`;
    case 5:
      return dapFrame('{"jsonrpc":');
    default: {
      const text = JSON.stringify({
        jsonrpc: '2.0',
        method: 'textDocument/publishDiagnostics',
        params: { uri: `file:///fuzz-${iteration}.cpp`, diagnostics: [] },
      });
      return dapFrame(text);
    }
  }
}

async function spawnHost(dataRoot) {
  const child = spawn(process.execPath, [hostScript], {
    cwd: root,
    env: { ...process.env, CODIUM_BLOCKS_DATA: dataRoot },
    stdio: ['pipe', 'pipe', 'pipe'],
  });
  const messages = [];
  const waiters = [];
  const lines = createInterface({ input: child.stdout, crlfDelay: Infinity });
  lines.on('line', (line) => {
    try {
      const message = JSON.parse(line);
      messages.push(message);
      for (let index = waiters.length - 1; index >= 0; index -= 1) {
        if (waiters[index].predicate(message)) {
          const waiter = waiters.splice(index, 1)[0];
          waiter.resolve(message);
        }
      }
    } catch (error) {
      for (const waiter of waiters.splice(0)) waiter.reject(error);
    }
  });
  const stderr = [];
  child.stderr.on('data', (chunk) => stderr.push(chunk.toString()));
  child.on('error', (error) => stderr.push(error.message));

  function waitFor(predicate, timeoutMs = 2000) {
    const existing = messages.find(predicate);
    if (existing) return Promise.resolve(existing);
    return new Promise((resolvePromise, reject) => {
      const timer = setTimeout(() => reject(new Error('Timed out waiting for Extension Host fuzz response')), timeoutMs);
      waiters.push({
        predicate,
        resolve: (message) => { clearTimeout(timer); resolvePromise(message); },
        reject: (error) => { clearTimeout(timer); reject(error); },
      });
    });
  }

  function sendRaw(line) {
    child.stdin.write(`${line}\n`);
  }

  function send(message) {
    sendRaw(JSON.stringify(message));
  }

  return { child, messages, stderr, waitFor, sendRaw, send };
}

async function runIteration(iteration, state) {
  const dataRoot = join(tempRoot, `data-${iteration}`);
  const payloadPath = join(tempRoot, `lsp-${iteration}.bin`);
  await mkdir(dataRoot, { recursive: true });
  await writeFile(payloadPath, lspPayload(state, iteration));
  const host = await spawnHost(dataRoot);
  try {
    const ready = await host.waitFor((message) => message.type === 'ready');
    assert.equal(ready.protocol, 2);
    assert.equal(ready.electron, false);

    const malformed = iteration % 5 === 0
      ? '{"id":1,"type":'
      : iteration % 5 === 1
        ? `{"id":1,"type":"unknown","payload":"${'x'.repeat((next(state) % 4096) + 1)}"}`
        : JSON.stringify({ id: 1, type: 'hello', fuzz: next(state) });
    host.sendRaw(malformed);
    const brokerResponse = await host.waitFor((message) => message.type === 'response' && (message.id === 1 || message.id === null));
    assert.equal(brokerResponse.type, 'response');

    host.send({ id: 2, type: 'startLanguageServer', command: process.execPath, args: [join(root, 'tests', 'fuzz-lsp-server.mjs'), payloadPath] });
    const started = await host.waitFor((message) => message.type === 'response' && message.id === 2);
    assert.equal(started.ok, true);

    const lspEvent = await host.waitFor((message) =>
      (message.type === 'event' && (message.event === 'languageServerMessage' ||
        message.event === 'languageServerError' || message.event === 'languageServerExit')),
      2500);
    assert.equal(lspEvent.type, 'event');

    host.send({ id: 3, type: 'stopLanguageServer' });
    const stopped = await host.waitFor((message) => message.type === 'response' && message.id === 3);
    assert.equal(stopped.ok, true);

    const exited = once(host.child, 'exit');
    host.send({ id: 4, type: 'shutdown' });
    const shutdown = await host.waitFor((message) => message.type === 'response' && message.id === 4);
    assert.equal(shutdown.shuttingDown, true);
    await Promise.race([
      exited,
      new Promise((_, reject) => setTimeout(() => reject(new Error('Extension Host did not exit after fuzz shutdown')), 2000)),
    ]);
  } finally {
    if (!host.child.killed && host.child.exitCode === null) host.child.kill('SIGTERM');
  }
}

await rm(tempRoot, { recursive: true, force: true });
await mkdir(tempRoot, { recursive: true });
const state = { value: seed };
for (let iteration = 0; iteration < iterations; iteration += 1) {
  await runIteration(iteration, state);
}
await rm(tempRoot, { recursive: true, force: true });
console.log(`fuzz-host-smoke: ok — bounded JSON Lines and LSP framing; seed=0x${seed.toString(16).toUpperCase()} iterations=${iterations}`);
