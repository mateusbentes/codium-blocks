import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { once } from 'node:events';
import { join, resolve } from 'node:path';

const root = resolve(process.env.CODIUM_BLOCKS_ROOT ?? new URL('..', import.meta.url).pathname);
const host = join(root, 'extension-host', 'src', 'host.mjs');
const demo = join(root, 'extensions', 'hello-codium');
const child = spawn(process.execPath, [host], { cwd: root, stdio: ['pipe', 'pipe', 'pipe'] });
const lines = createInterface({ input: child.stdout, crlfDelay: Infinity });
const messages = [];
const waiters = [];

lines.on('line', (line) => {
  const message = JSON.parse(line);
  messages.push(message);
  for (let i = waiters.length - 1; i >= 0; i -= 1) {
    if (waiters[i].predicate(message)) {
      const waiter = waiters.splice(i, 1)[0];
      waiter.resolve(message);
    }
  }
});

child.stderr.on('data', (data) => process.stderr.write(data));

function waitFor(predicate, timeoutMs = 3000) {
  const existing = messages.find(predicate);
  if (existing) return Promise.resolve(existing);
  return new Promise((resolvePromise, reject) => {
    const timer = setTimeout(() => reject(new Error('Timed out waiting for a host message')), timeoutMs);
    waiters.push({
      predicate,
      resolve: (message) => { clearTimeout(timer); resolvePromise(message); },
    });
  });
}

function send(message) {
  child.stdin.write(`${JSON.stringify(message)}\n`);
}

try {
  await waitFor((message) => message.type === 'ready');
  send({ id: 1, type: 'hello' });
  const hello = await waitFor((message) => message.type === 'response' && message.id === 1);
  assert.equal(hello.ok, true);
  assert.equal(hello.electron, false);

  send({ id: 2, type: 'load', extensionPath: demo });
  const loaded = await waitFor((message) => message.type === 'response' && message.id === 2);
  assert.equal(loaded.ok, true);
  assert.equal(loaded.extension.id, 'codium-blocks.hello-codium');
  assert.deepEqual(loaded.extension.commands, ['hello.codium']);

  send({ id: 3, type: 'executeCommand', command: 'hello.codium' });
  const executed = await waitFor((message) => message.type === 'response' && message.id === 3);
  assert.equal(executed.ok, true);
  assert.equal(executed.result, 'hello-from-extension');
  await waitFor((message) => message.type === 'event' && message.event === 'notification');

  send({ id: 4, type: 'listExtensions' });
  const listed = await waitFor((message) => message.type === 'response' && message.id === 4);
  assert.equal(listed.extensions[0].id, 'codium-blocks.hello-codium');

  send({ id: 5, type: 'shutdown' });
  await waitFor((message) => message.type === 'response' && message.id === 5);
  await once(child, 'exit');
  console.log('host-smoke: ok — Node.js Extension Host without Electron');
} finally {
  if (!child.killed) child.kill('SIGTERM');
}
