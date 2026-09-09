#!/usr/bin/env node

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
      body: { supportsConfigurationDoneRequest: true, supportsTerminateRequest: true } });
  } else if (request.command === 'launch') {
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command });
  } else if (request.command === 'configurationDone') {
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command });
  } else if (request.command === 'threads') {
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command,
      body: { threads: [{ id: 1, name: 'main' }] } });
  } else if (request.command === 'continue') {
    send({ type: 'response', request_seq: request.seq, success: true, command: request.command,
      body: { allThreadsContinued: true } });
  } else if (request.command === 'disconnect') {
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
