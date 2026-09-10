import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { once } from 'node:events';
import { readFile, rm } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const configuredRoot = process.env.CODIUM_BLOCKS_ROOT;
const root = configuredRoot
  ? resolve(configuredRoot)
  : fileURLToPath(new URL('../', import.meta.url));
const host = join(root, 'extension-host', 'src', 'host.mjs');
const demo = join(root, 'extensions', 'hello-codium');
const dataRoot = process.env.CODIUM_BLOCKS_DATA ?? join(root, 'build', 'test-data');
await rm(dataRoot, { recursive: true, force: true });
const child = spawn(process.execPath, [host], {
  cwd: root,
  env: { ...process.env, CODIUM_BLOCKS_DATA: dataRoot },
  stdio: ['pipe', 'pipe', 'pipe'],
});
child.on('error', (error) => {
  process.stderr.write(`host-smoke: failed to spawn Extension Host: ${error.message}\n`);
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

  const fakeLsp = join(root, 'tests', 'fake-lsp.mjs');
  send({ id: 8, type: 'startLanguageServer', command: process.execPath, args: [fakeLsp] });
  const fakeStarted = await waitFor((message) => message.type === 'response' && message.id === 8);
  assert.equal(fakeStarted.ok, true);

  send({ id: 9, type: 'languageServerRequest', message: {
    jsonrpc: '2.0', id: 101, method: 'initialize', params: { rootUri: 'file:///workspace' },
  } });
  const initializeSent = await waitFor((message) => message.type === 'response' && message.id === 9);
  assert.equal(initializeSent.sent, true);
  const initialized = await waitFor((message) => message.type === 'event' &&
    message.event === 'languageServerMessage' && message.message.id === 101);
  assert.equal(initialized.message.result.capabilities.hoverProvider, true);
  await waitFor((message) => message.type === 'event' && message.event === 'languageServerResult' &&
    message.method === 'initialize');
  assert.deepEqual(initialized.message.result.capabilities.semanticTokensProvider.legend.tokenTypes,
    ['type', 'function', 'keyword', 'number']);

  send({ id: 10, type: 'languageServerNotification', message: {
    jsonrpc: '2.0', method: 'textDocument/didOpen', params: {
      textDocument: { uri: 'file:///workspace/main.cpp', languageId: 'cpp', version: 1, text: 'int main() {}' },
    },
  } });
  const opened = await waitFor((message) => message.type === 'response' && message.id === 10);
  assert.equal(opened.sent, true);
  const diagnostics = await waitFor((message) => message.type === 'event' &&
    message.event === 'languageServerMessage' && message.message.method === 'textDocument/publishDiagnostics');
  assert.equal(diagnostics.message.params.diagnostics[0].source, 'codium-blocks-fake-lsp');
  const normalizedDiagnostics = await waitFor((message) => message.type === 'event' && message.event === 'diagnostics');
  assert.equal(normalizedDiagnostics.diagnostics[0].severity, 2);

  send({ id: 11, type: 'languageServerRequest', message: {
    jsonrpc: '2.0', id: 102, method: 'textDocument/hover', params: {},
  } });
  await waitFor((message) => message.type === 'response' && message.id === 11);
  const hover = await waitFor((message) => message.type === 'event' &&
    message.event === 'languageServerMessage' && message.message.id === 102);
  assert.equal(hover.message.result.contents[0].value, 'Hover response from fake LSP');
  const normalizedHover = await waitFor((message) => message.type === 'event' && message.event === 'languageServerResult' &&
    message.method === 'textDocument/hover');
  assert.equal(normalizedHover.result.contents[0].value, 'Hover response from fake LSP');

  send({ id: 12, type: 'languageServerRequest', message: {
    jsonrpc: '2.0', id: 104, method: 'textDocument/semanticTokens/full',
    params: { textDocument: { uri: 'file:///workspace/main.cpp' } },
  } });
  await waitFor((message) => message.type === 'response' && message.id === 12);
  const semanticTokens = await waitFor((message) => message.type === 'event' &&
    message.event === 'languageServerMessage' && message.message.id === 104);
  assert.deepEqual(semanticTokens.message.result.data,
    [0, 0, 3, 0, 0, 0, 4, 4, 1, 0, 1, 0, 6, 2, 0, 0, 7, 2, 3, 0]);
  const normalizedSemanticTokens = await waitFor((message) => message.type === 'event' &&
    message.event === 'languageServerResult' && message.method === 'textDocument/semanticTokens/full');
  assert.equal(normalizedSemanticTokens.result.data[3], 0);

  send({ id: 13, type: 'languageServerRequest', message: {
    jsonrpc: '2.0', id: 103, method: 'textDocument/completion', params: {},
  } });
  await waitFor((message) => message.type === 'response' && message.id === 13);
  const completion = await waitFor((message) => message.type === 'event' &&
    message.event === 'languageServerMessage' && message.message.id === 103);
  assert.equal(completion.message.result.items[0].label, 'codiumBlocksCompletion');
  const normalizedCompletion = await waitFor((message) => message.type === 'event' && message.event === 'languageServerResult' &&
    message.method === 'textDocument/completion');
  assert.equal(normalizedCompletion.result.items[0].label, 'codiumBlocksCompletion');

  send({ id: 14, type: 'stopLanguageServer' });
  const fakeStopped = await waitFor((message) => message.type === 'response' && message.id === 14);
  assert.equal(fakeStopped.stopped, true);

  send({ id: 15, type: 'shutdown' });
  await waitFor((message) => message.type === 'response' && message.id === 15);
  await once(child, 'exit');
  console.log('host-smoke: ok — commands, configuration, contributions, and LSP process manager without Electron');
} finally {
  if (!child.killed) child.kill('SIGTERM');
}
