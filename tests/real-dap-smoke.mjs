#!/usr/bin/env node

import { readFile } from 'node:fs/promises';
import { spawn } from 'node:child_process';

const matrix = JSON.parse(await readFile(new URL('./real-dap-matrix.json', import.meta.url), 'utf8'));
const adapters = matrix.adapters.filter((adapter) => adapter.platforms.includes(process.platform));
const WINDOWS_DLL_NOT_FOUND = 0xC0000135;

function send(child, message) {
  const body = JSON.stringify(message);
  child.stdin.write(`Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n${body}`);
}

function probe(adapter) {
  return new Promise((resolve, reject) => {
    const command = adapter.command[process.platform];
    if (!command) {
      resolve({ skipped: true, reason: 'no command for platform' });
      return;
    }
    const child = spawn(command, adapter.args, { stdio: ['pipe', 'pipe', 'pipe'] });
    let buffer = Buffer.alloc(0);
    let stderr = '';
    let settled = false;
    const finish = (callback, value) => {
      if (settled) return;
      settled = true;
      clearTimeout(timeout);
      child.removeAllListeners();
      try { child.kill(); } catch {}
      callback(value);
    };
    const timeout = setTimeout(() => finish(reject, new Error(`${adapter.id}: timeout waiting for DAP initialize`)), 12000);
    child.on('error', (error) => {
      if (error.code === 'ENOENT') finish(resolve, { skipped: true, reason: 'executable not installed' });
      else finish(reject, error);
    });
    child.on('exit', (code, signal) => {
      if (settled) return;
      if (process.platform === 'win32' && code === WINDOWS_DLL_NOT_FOUND) {
        finish(resolve, { skipped: true, reason: 'executable could not load a required Windows DLL' });
        return;
      }
      finish(reject, new Error(`${adapter.id}: exited before initialize (code=${code}, signal=${signal ?? ''}, stderr=${stderr.trim()})`));
    });
    child.stderr.on('data', (chunk) => { stderr = `${stderr}${chunk.toString()}`.slice(-8192); });
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
        let message;
        try { message = JSON.parse(body); } catch { continue; }
        if (message.type === 'response' && message.request_seq === 1 && message.command === 'initialize') {
          if (message.success === false) {
            finish(reject, new Error(`${adapter.id}: initialize rejected: ${JSON.stringify(message.message ?? message.body ?? message)}`));
          } else {
            try { send(child, { seq: 2, type: 'request', command: 'disconnect', arguments: { terminateDebuggee: false } }); } catch {}
            finish(resolve, { skipped: false, capabilities: message.body ?? {} });
          }
          return;
        }
      }
    });
    send(child, {
      seq: 1,
      type: 'request',
      command: 'initialize',
      arguments: {
        clientID: 'codium-blocks',
        clientName: 'Codium::Blocks real DAP matrix',
        adapterID: adapter.id,
        linesStartAt1: true,
        columnsStartAt1: true,
        pathFormat: 'path',
        supportsVariableType: true,
        supportsVariablePaging: true,
      },
    });
  });
}

let executed = 0;
for (const adapter of adapters) {
  const result = await probe(adapter);
  if (result.skipped) {
    console.log(`real-dap-smoke: ${adapter.id} skipped — ${result.reason}`);
  } else {
    executed += 1;
    console.log(`real-dap-smoke: ${adapter.id} ok — DAP initialize (${adapter.version})`);
  }
}
if (executed === 0) console.log('real-dap-smoke: no installed adapter completed; optional runtime probe skipped');
