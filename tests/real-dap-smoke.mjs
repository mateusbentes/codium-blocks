#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors


import { readFile } from 'node:fs/promises';
import { dirname } from 'node:path';
import { spawn } from 'node:child_process';

const matrix = JSON.parse(await readFile(new URL('./real-dap-matrix.json', import.meta.url), 'utf8'));
const adapters = matrix.adapters
  .filter((adapter) => adapter.platforms.includes(process.platform))
  .map((adapter) => ({
    ...adapter,
    version: typeof adapter.version === 'string' ? adapter.version : adapter.version?.[process.platform],
    versionPattern: typeof adapter.versionPattern === 'string'
      ? adapter.versionPattern : adapter.versionPattern?.[process.platform],
    versionCommand: adapter.versionCommand?.[process.platform] ?? adapter.command[process.platform],
  }));
const WINDOWS_DLL_NOT_FOUND = 0xC0000135;
const debuggee = process.env.CODIUM_BLOCKS_REAL_DAP_DEBUGGEE ?? '';
const sourceFile = process.env.CODIUM_BLOCKS_REAL_DAP_SOURCE ?? '';

function send(child, message) {
  const body = JSON.stringify(message);
  child.stdin.write(`Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n${body}`);
}

class DapSession {
  constructor(child, adapter) {
    this.child = child;
    this.adapter = adapter;
    this.buffer = Buffer.alloc(0);
    this.messages = [];
    this.waiters = [];
    this.stderr = '';
    this.nextSequence = 1;
    this.closed = false;
    child.stdout.on('data', (chunk) => this.onData(chunk));
    child.stderr.on('data', (chunk) => { this.stderr = `${this.stderr}${chunk.toString()}`.slice(-8192); });
    child.on('exit', (code, signal) => {
      this.closed = true;
      for (const waiter of this.waiters.splice(0)) {
        clearTimeout(waiter.timeout);
        waiter.reject(new Error(`${this.adapter.id}: exited during ${waiter.phase} (code=${code}, signal=${signal ?? ''}, stderr=${this.stderr.trim()})`));
      }
    });
  }

  onData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (true) {
      const separator = this.buffer.indexOf(Buffer.from('\r\n\r\n'));
      if (separator < 0) return;
      const header = this.buffer.subarray(0, separator).toString('ascii');
      const match = header.match(/Content-Length:\s*(\d+)/i);
      if (!match) {
        this.buffer = this.buffer.subarray(separator + 4);
        continue;
      }
      const bodyStart = separator + 4;
      const length = Number(match[1]);
      if (this.buffer.length < bodyStart + length) return;
      const body = this.buffer.subarray(bodyStart, bodyStart + length).toString('utf8');
      this.buffer = this.buffer.subarray(bodyStart + length);
      try { this.dispatch(JSON.parse(body)); } catch {}
    }
  }

  dispatch(message) {
    if (process.env.CODIUM_BLOCKS_REAL_DAP_TRACE === '1') {
      console.error(`real-dap-trace: ${JSON.stringify(message)}`);
    }
    const index = this.waiters.findIndex((waiter) => waiter.predicate(message));
    if (index >= 0) {
      const waiter = this.waiters.splice(index, 1)[0];
      clearTimeout(waiter.timeout);
      waiter.resolve(message);
    } else {
      this.messages.push(message);
    }
  }

  waitFor(predicate, phase, timeoutMs = 15000) {
    const queued = this.messages.findIndex(predicate);
    if (queued >= 0) return Promise.resolve(this.messages.splice(queued, 1)[0]);
    if (this.closed) return Promise.reject(new Error(`${this.adapter.id}: adapter closed during ${phase}`));
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.waiters = this.waiters.filter((waiter) => waiter.resolve !== resolve);
        reject(new Error(`${this.adapter.id}: timeout during ${phase}; stderr=${this.stderr.trim()}`));
      }, timeoutMs);
      this.waiters.push({ predicate, phase, resolve, reject, timeout });
    });
  }

  async request(command, args = {}) {
    const seq = this.nextSequence++;
    if (process.env.CODIUM_BLOCKS_REAL_DAP_TRACE === '1') {
      console.error(`real-dap-request: ${JSON.stringify({ seq, command, arguments: args })}`);
    }
    const response = this.waitFor(
      (message) => message.type === 'response' && message.request_seq === seq,
      command,
    );
    send(this.child, { seq, type: 'request', command, arguments: args });
    const result = await response;
    if (result.success === false) {
      throw new Error(`${this.adapter.id}: ${command} rejected: ${JSON.stringify(result.message ?? result.body ?? result)}`);
    }
    return result;
  }

  async event(names, phase) {
    return this.waitFor(
      (message) => message.type === 'event' && names.includes(message.event),
      phase,
    );
  }
}

function supports(capabilities, name) {
  const value = capabilities?.[name];
  return value === true || (value !== false && value !== undefined);
}

async function verifyVersion(adapter, command) {
  if (!adapter.versionArgs || !adapter.versionPattern) return;
  const child = spawn(adapter.versionCommand, adapter.versionArgs, {
    stdio: ['ignore', 'pipe', 'pipe'],
    shell: process.platform === 'win32' && adapter.versionCommand.endsWith('.cmd'),
  });
  let output = '';
  child.stdout.on('data', (chunk) => { output += chunk.toString(); });
  child.stderr.on('data', (chunk) => { output += chunk.toString(); });
  const code = await new Promise((resolve, reject) => {
    child.once('error', reject);
    child.once('close', resolve);
  });
  if (!new RegExp(adapter.versionPattern, 'i').test(output)) {
    throw new Error(`${adapter.id}: installed version does not match ${adapter.version}; code=${code}, output=${output.trim()}`);
  }
}

async function closeSession(session) {
  if (!session || session.closed) return;
  try {
    const request = session.request('disconnect', { terminateDebuggee: false });
    await Promise.race([request, new Promise((resolve) => setTimeout(resolve, 3000))]);
  } catch {}
  try { session.child.stdin.end(); } catch {}
  await new Promise((resolve) => {
    if (session.child.exitCode !== null || session.child.signalCode !== null) return resolve();
    const timer = setTimeout(() => { try { session.child.kill(); } catch {} resolve(); }, 3000);
    session.child.once('close', () => { clearTimeout(timer); resolve(); });
  });
}

async function probe(adapter) {
  const command = adapter.command[process.platform];
  if (!command) return { skipped: true, reason: 'no command for platform' };
  await verifyVersion(adapter, command);
  const child = spawn(command, adapter.args, {
    cwd: debuggee ? dirname(debuggee) : undefined,
    stdio: ['pipe', 'pipe', 'pipe'],
    shell: process.platform === 'win32' && command.endsWith('.cmd'),
  });
  let startupError;
  child.once('error', (error) => { startupError = error; });
  await new Promise((resolve) => setImmediate(resolve));
  if (startupError?.code === 'ENOENT') {
    try { child.kill(); } catch {}
    return { skipped: true, reason: 'executable not installed' };
  }
  const session = new DapSession(child, adapter);
  try {
    const initialized = await session.request('initialize', {
      clientID: 'codium-blocks',
      clientName: 'Codium::Blocks real DAP matrix',
      adapterID: adapter.id,
      linesStartAt1: true,
      columnsStartAt1: true,
      pathFormat: 'path',
      supportsVariableType: true,
      supportsVariablePaging: true,
    });
    const capabilities = initialized.body ?? {};
    if (adapter.scenario === 'initialize-only' || !debuggee || !sourceFile) {
      return { skipped: false, scenario: 'initialize-only', capabilities };
    }
    if (adapter.requiresInitializedEvent !== false) {
      await session.event(['initialized'], 'adapter initialized');
    }

    const result = {
      scenario: adapter.scenario,
      capabilities,
      sourceBreakpoint: [],
      functionBreakpoint: 'unsupported',
      dataBreakpoint: 'unsupported',
      stopped: false,
      stackTrace: false,
      scopes: false,
      variables: false,
      continued: false,
    };
    await session.request('launch', {
      program: debuggee,
      cwd: dirname(debuggee),
      stopOnEntry: true,
      stopAtBeginningOfMainSubprogram: true,
      noDebug: false,
    });
    if (supports(capabilities, 'supportsConfigurationDoneRequest')) await session.request('configurationDone');
    const firstEvent = await session.event(['stopped', 'terminated', 'exited'], 'initial stop');
    if (firstEvent.event !== 'stopped') {
      throw new Error(`${adapter.id}: full scenario ended before a stopped event (${firstEvent.event})`);
    }
    result.stopped = true;

    const sourceBreakpoint = await session.request('setBreakpoints', {
      source: { path: sourceFile },
      breakpoints: [{ line: adapter.breakpointLine, condition: 'global_counter >= 0', hitCondition: '1' }],
      sourceModified: false,
    });
    result.sourceBreakpoint = sourceBreakpoint.body?.breakpoints ?? [];
    if (supports(capabilities, 'supportsFunctionBreakpoints')) {
      const response = await session.request('setFunctionBreakpoints', {
        breakpoints: [{ name: 'helper', condition: 'global_counter >= 0', hitCondition: '1' }],
      });
      result.functionBreakpoint = response.body?.breakpoints ?? [];
    }
    if (supports(capabilities, 'supportsDataBreakpoints')) {
      const response = await session.request('setDataBreakpoints', {
        breakpoints: [{ dataId: 'global_counter', accessType: 'write' }],
      });
      result.dataBreakpoint = response.body?.breakpoints ?? [];
    }

    const threads = await session.request('threads');
    const threadId = threads.body?.threads?.[0]?.id;
    if (!threadId) throw new Error(`${adapter.id}: threads response contained no thread`);
    const stack = await session.request('stackTrace', { threadId, startFrame: 0, levels: 20 });
    const frame = stack.body?.stackFrames?.[0];
    if (frame?.id === undefined || frame?.id === null) {
      throw new Error(`${adapter.id}: stackTrace response contained no frame`);
    }
    result.stackTrace = true;
    const scopes = await session.request('scopes', { frameId: frame.id });
    const scope = scopes.body?.scopes?.find((item) => item.variablesReference > 0);
    if (scope) {
      result.scopes = true;
      const variables = await session.request('variables', { variablesReference: scope.variablesReference });
      result.variables = Array.isArray(variables.body?.variables);
    }
    await session.request('continue', { threadId });
    const finalEvent = await session.event(['stopped', 'terminated', 'exited'], 'continued execution');
    result.continued = finalEvent.event === 'stopped' || finalEvent.event === 'terminated' || finalEvent.event === 'exited';
    return result;
  } finally {
    await closeSession(session);
  }
}

let executed = 0;
for (const adapter of adapters) {
  const result = await probe(adapter).catch((error) => {
    if (error?.code === 'ENOENT' || String(error?.message).includes('ENOENT')) {
      return { skipped: true, reason: 'executable not installed' };
    }
    if (process.platform === 'win32' && String(error?.message).includes(`code=${WINDOWS_DLL_NOT_FOUND}`)) {
      return { skipped: true, reason: 'executable could not load a required Windows DLL' };
    }
    throw error;
  });
  if (result.skipped) {
    console.log(`real-dap-smoke: ${adapter.id} skipped — ${result.reason}`);
  } else {
    executed += 1;
    console.log(`real-dap-smoke: ${adapter.id} ok — ${result.scenario ?? 'initialize-only'} ${JSON.stringify(result)}`);
  }
}
if (executed === 0) console.log('real-dap-smoke: no installed adapter completed; optional runtime probe skipped');
