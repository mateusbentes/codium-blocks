#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors


let buffer = Buffer.alloc(0);

function send(message) {
  const body = JSON.stringify(message);
  process.stdout.write(`Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n${body}`);
}

function handle(request) {
  if (request.type !== 'request') return;
  if (request.command === 'initialize') {
    send({ type: 'event', event: 'initialized', body: {} });
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command,
      body: {
        supportsConfigurationDoneRequest: true,
        supportsTerminateRequest: true,
        supportsFunctionBreakpoints: true,
        supportsDataBreakpoints: true,
      } });
  } else if (request.command === 'launch') {
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command });
  } else if (request.command === 'configurationDone') {
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command });
  } else if (request.command === 'threads') {
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command,
      body: { threads: [{ id: 1, name: 'main' }] } });
  } else if (request.command === 'setBreakpoints') {
    const first = request.arguments?.breakpoints?.[0] ?? {};
    // Keep the transport smoke deterministic while proving the native payload.
    const optionsAccepted = first.condition === 'counter > 0' && first.hitCondition === '3' &&
      first.logMessage === 'counter=%d';
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command,
      body: { optionsAccepted, breakpoints: (request.arguments?.breakpoints ?? []).map((item, index) => ({ id: index + 1, verified: true, line: item.line })) } });
  } else if (request.command === 'setFunctionBreakpoints') {
    const first = request.arguments?.breakpoints?.[0] ?? {};
    const optionsAccepted = first.name === 'main' && first.condition === 'counter > 0' && first.hitCondition === '3';
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command,
      body: { optionsAccepted, breakpoints: (request.arguments?.breakpoints ?? []).map((item, index) => ({ id: index + 10, verified: true, message: item.name })) } });
  } else if (request.command === 'setDataBreakpoints') {
    const first = request.arguments?.breakpoints?.[0] ?? {};
    const optionsAccepted = first.dataId === 'counter' && first.accessType === 'write';
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command,
      body: { optionsAccepted, breakpoints: (request.arguments?.breakpoints ?? []).map((item, index) => ({ id: index + 20, verified: true, message: item.dataId })) } });
  } else if (request.command === 'stackTrace') {
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command,
      body: { stackFrames: [{ id: 7, name: 'main', line: 12, column: 1, source: { path: 'demo.cpp' } }] } });
  } else if (request.command === 'scopes') {
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command,
      body: { scopes: [{ name: 'Locals', variablesReference: 42, expensive: false }] } });
  } else if (request.command === 'variables') {
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command,
      body: { variables: [{ name: 'answer', value: '42', type: 'int', variablesReference: 0 }] } });
  } else if (request.command === 'evaluate') {
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command,
      body: { result: '42', type: 'int', variablesReference: 0 } });
  } else if (request.command === 'continue') {
    send({ type: 'event', event: 'continued', body: { threadId: 1, allThreadsContinued: true } });
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command,
      body: { allThreadsContinued: true } });
  } else if (request.command === 'pause') {
    send({ type: 'event', event: 'stopped', body: { reason: 'pause', threadId: 1, allThreadsStopped: true } });
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command });
  } else if (request.command === 'disconnect') {
    send({ type: 'event', event: 'terminated', body: {} });
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command });
    process.exit(0);
  } else {
    send({ type: 'response', request_seq: request.seq, success: false, command: request.command,
      message: `Unsupported command: ${request.command}` });
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
