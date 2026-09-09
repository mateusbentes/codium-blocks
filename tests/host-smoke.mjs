import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { once } from 'node:events';
import { readFile, rm } from 'node:fs/promises';
import { join, resolve } from 'node:path';

const root = resolve(process.env.CODIUM_BLOCKS_ROOT ?? new URL('..', import.meta.url).pathname);
const host = join(root, 'extension-host', 'src', 'host.mjs');
const demo = join(root, 'extensions', 'hello-codium');
const dataRoot = process.env.CODIUM_BLOCKS_DATA ?? join(root, 'build', 'test-data');
await rm(dataRoot, { recursive: true, force: true });
const child = spawn(process.execPath, [host], {
  cwd: root,
  env: { ...process.env, CODIUM_BLOCKS_DATA: dataRoot },
  stdio: ['pipe', 'pipe', 'pipe'],
});
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
  const ready = await waitFor((message) => message.type === 'ready');
  assert.equal(ready.electron, false);
  assert.equal(ready.protocol, 2);
  assert.equal(ready.capabilities.includes('configuration'), true);
  assert.equal(ready.capabilities.includes('lsp-process-manager'), true);

  send({ id: 1, type: 'hello' });
  const hello = await waitFor((message) => message.type === 'response' && message.id === 1);
  assert.equal(hello.ok, true);
  assert.equal(hello.electron, false);
  assert.equal(hello.lsp, true);

  send({ id: 2, type: 'load', extensionPath: demo });
  const loaded = await waitFor((message) => message.type === 'response' && message.id === 2);
  assert.equal(loaded.ok, true);
  assert.equal(loaded.extension.id, 'codium-blocks.hello-codium');
  assert.deepEqual(loaded.extension.commands, ['hello.codium', 'hello.codium.configure']);
  assert.equal(loaded.extension.contributes.commands.length, 2);

  send({ id: 3, type: 'executeCommand', command: 'hello.codium' });
  const executed = await waitFor((message) => message.type === 'response' && message.id === 3);
  assert.equal(executed.ok, true);
  assert.equal(executed.result, 'hello-from-extension');
  await waitFor((message) => message.type === 'event' && message.event === 'notification');

  send({ id: 4, type: 'executeCommand', command: 'hello.codium.configure' });
  const configured = await waitFor((message) => message.type === 'response' && message.id === 4);
  assert.equal(configured.ok, true);
  assert.equal(configured.result, 'configuration-updated');
  await waitFor((message) => message.type === 'event' && message.event === 'configurationChanged');
  const storedSettings = JSON.parse(await readFile(join(dataRoot, 'settings.json'), 'utf8'));
  assert.equal(storedSettings['helloCodium.greeting'], 'Configuration updated in Codium::Blocks');

  send({ id: 5, type: 'listExtensions' });
  const listed = await waitFor((message) => message.type === 'response' && message.id === 5);
  assert.equal(listed.extensions[0].id, 'codium-blocks.hello-codium');

  send({ id: 6, type: 'startLanguageServer', command: process.execPath, args: ['-e', 'process.stdin.resume()'] });
  const languageServer = await waitFor((message) => message.type === 'response' && message.id === 6);
  assert.equal(languageServer.ok, true);
  assert.equal(languageServer.languageServer.command, process.execPath);

  send({ id: 7, type: 'stopLanguageServer' });
  const stopped = await waitFor((message) => message.type === 'response' && message.id === 7);
  assert.equal(stopped.ok, true);
  assert.equal(stopped.stopped, true);

  send({ id: 8, type: 'shutdown' });
  await waitFor((message) => message.type === 'response' && message.id === 8);
  await once(child, 'exit');
  console.log('host-smoke: ok — commands, configuration, contributions, and LSP process manager without Electron');
} finally {
  if (!child.killed) child.kill('SIGTERM');
}
