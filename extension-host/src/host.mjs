#!/usr/bin/env node

import { createRequire } from 'node:module';
import { createInterface } from 'node:readline';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { homedir } from 'node:os';
import { fileURLToPath, pathToFileURL } from 'node:url';
import Module from 'node:module';

const originalLoad = Module._load;
const extensions = new Map();
const configurationListeners = new Set();
const configuration = new Map();
let activeExtension = null;
let languageServer = null;

const defaultDataRoot = process.platform === 'win32'
  ? join(process.env.APPDATA || homedir(), 'CodiumBlocks')
  : process.platform === 'darwin'
    ? join(homedir(), 'Library', 'Application Support', 'CodiumBlocks')
    : join(process.env.XDG_STATE_HOME || join(homedir(), '.local', 'state'), 'codium-blocks');
const dataRoot = process.env.CODIUM_BLOCKS_DATA ?? defaultDataRoot;
const configurationFile = join(dataRoot, 'settings.json');

async function loadConfiguration() {
  try {
    const stored = JSON.parse(await readFile(configurationFile, 'utf8'));
    for (const [key, value] of Object.entries(stored)) configuration.set(key, value);
  } catch {
    // A missing or malformed local settings file starts with an empty configuration.
  }
}

async function persistConfiguration() {
  await mkdir(dataRoot, { recursive: true });
  await writeFile(configurationFile, `${JSON.stringify(Object.fromEntries(configuration), null, 2)}\n`, 'utf8');
}

function send(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

function response(request, result = {}) {
  send({ type: 'response', id: request.id ?? null, ok: true, ...result });
}

function failure(request, error) {
  send({
    type: 'response',
    id: request.id ?? null,
    ok: false,
    error: error instanceof Error ? error.message : String(error),
  });
}

function currentExtension() {
  if (!activeExtension) {
    throw new Error('No extension is currently being activated.');
  }
  return activeExtension;
}

function disposable(dispose) {
  return { dispose };
}

function configurationKey(section, key) {
  return section ? `${section}.${key}` : key;
}

function configurationValue(section, key, defaultValue) {
  const fullKey = configurationKey(section, key);
  if (configuration.has(fullKey)) return configuration.get(fullKey);
  for (const extension of extensions.values()) {
    const property = extension.packageJSON.contributes?.configuration?.properties?.[fullKey];
    if (property && Object.hasOwn(property, 'default')) return property.default;
  }
  return defaultValue;
}

const vscode = {
  version: '0.2.0-codium-blocks',
  env: {
    appName: 'Codium::Blocks',
    appHost: 'desktop-native',
    language: 'en-US',
  },
  commands: {
    registerCommand(command, callback) {
      const extension = currentExtension();
      extension.commands.set(command, callback);
      return disposable(() => extension.commands.delete(command));
    },
    async executeCommand(command, ...args) {
      for (const extension of extensions.values()) {
        const callback = extension.commands.get(command);
        if (callback) return callback(...args);
      }
      throw new Error(`Command not found: ${command}`);
    },
  },
  window: {
    showInformationMessage(message, ...items) {
      send({ type: 'event', event: 'notification', level: 'info', message: String(message) });
      return Promise.resolve(items[0]);
    },
    showWarningMessage(message, ...items) {
      send({ type: 'event', event: 'notification', level: 'warning', message: String(message) });
      return Promise.resolve(items[0]);
    },
    showErrorMessage(message, ...items) {
      send({ type: 'event', event: 'notification', level: 'error', message: String(message) });
      return Promise.resolve(items[0]);
    },
    createOutputChannel(name) {
      return {
        name,
        appendLine(value) {
          send({ type: 'event', event: 'output', channel: name, message: String(value) });
        },
        append(value) {
          send({ type: 'event', event: 'output', channel: name, message: String(value) });
        },
        show() {},
        dispose() {},
      };
    },
    createStatusBarItem() {
      return { text: '', tooltip: '', show() {}, hide() {}, dispose() {} };
    },
  },
  workspace: {
    workspaceFolders: [],
    getConfiguration(section = '') {
      return {
        get(key, defaultValue) { return configurationValue(section, key, defaultValue); },
        has(key) { return configuration.has(configurationKey(section, key)); },
        inspect(key) {
          const fullKey = configurationKey(section, key);
          return { key: fullKey, defaultValue: configurationValue(section, key, undefined), globalValue: configuration.get(fullKey) };
        },
        async update(key, value) {
          const fullKey = configurationKey(section, key);
          configuration.set(fullKey, value);
          await persistConfiguration();
          for (const listener of configurationListeners) listener({ affectsConfiguration: (candidate) => candidate === fullKey });
          send({ type: 'event', event: 'configurationChanged', key: fullKey, value });
        },
        section,
      };
    },
    onDidChangeConfiguration(listener) {
      configurationListeners.add(listener);
      return disposable(() => configurationListeners.delete(listener));
    },
  },
  languages: {
    registerCompletionItemProvider() { return disposable(() => {}); },
    registerDefinitionProvider() { return disposable(() => {}); },
    registerHoverProvider() { return disposable(() => {}); },
    registerDocumentFormattingEditProvider() { return disposable(() => {}); },
  },
  extensions: {
    getExtension(id) {
      return extensions.get(id);
    },
    all: [],
  },
  Uri: {
    file(filePath) {
      const absolute = resolve(filePath);
      return { scheme: 'file', fsPath: absolute, path: absolute, toString: () => pathToFileURL(absolute).toString() };
    },
  },
};

// Compatibility bridge for CommonJS VS Code extensions. A future version can
// replace this with a capability-scoped loader and a worker pool.
Module._load = function patchedLoad(request, parent, isMain) {
  if (request === 'vscode') return vscode;
  return originalLoad.call(this, request, parent, isMain);
};

function parseLanguageServerFrames(state, chunk) {
  state.buffer = Buffer.concat([state.buffer, chunk]);
  while (true) {
    const separator = state.buffer.indexOf(Buffer.from('\r\n\r\n'));
    if (separator < 0) return;
    const header = state.buffer.subarray(0, separator).toString('ascii');
    const match = header.match(/Content-Length:\s*(\d+)/i);
    if (!match) {
      state.buffer = state.buffer.subarray(separator + 4);
      continue;
    }
    const bodyStart = separator + 4;
    const length = Number(match[1]);
    if (state.buffer.length < bodyStart + length) return;
    const body = state.buffer.subarray(bodyStart, bodyStart + length).toString('utf8');
    state.buffer = state.buffer.subarray(bodyStart + length);
    try {
      send({ type: 'event', event: 'languageServerMessage', message: JSON.parse(body) });
    } catch {
      send({ type: 'event', event: 'languageServerMessage', message: body });
    }
  }
}

function sendLanguageServerMessage(message) {
  if (!languageServer?.child?.stdin?.writable) {
    throw new Error('No language server is running.');
  }
  const body = JSON.stringify(message);
  const header = `Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n`;
  languageServer.child.stdin.write(header + body);
}

function stopLanguageServer() {
  if (!languageServer) return false;
  languageServer.child.kill();
  languageServer = null;
  return true;
}

function startLanguageServer(command, args = [], cwd = process.cwd()) {
  stopLanguageServer();
  const child = spawn(command, args, { cwd, stdio: ['pipe', 'pipe', 'pipe'] });
  const state = { child, buffer: Buffer.alloc(0), command };
  languageServer = state;
  child.stdout.on('data', (chunk) => parseLanguageServerFrames(state, chunk));
  child.stderr.on('data', (chunk) => send({ type: 'event', event: 'languageServerStderr', message: chunk.toString() }));
  child.on('error', (error) => send({ type: 'event', event: 'languageServerError', message: error.message }));
  child.on('exit', (code, signal) => {
    send({ type: 'event', event: 'languageServerExit', command, code, signal });
    if (languageServer === state) languageServer = null;
  });
  return { command, args, cwd };
}

async function loadExtension(extensionPath) {
  const root = resolve(extensionPath);
  const manifest = JSON.parse(await readFile(join(root, 'package.json'), 'utf8'));
  if (!manifest.name || !manifest.publisher) {
    throw new Error('Invalid manifest: name and publisher are required.');
  }
  if (manifest.engines?.vscode && manifest.engines.vscode === '*') {
    throw new Error('Manifests with engines.vscode=* are not accepted by this host.');
  }

  const id = `${manifest.publisher}.${manifest.name}`;
  const entry = join(root, manifest.main ?? 'extension.js');
  const extension = {
    id,
    extensionPath: root,
    packageJSON: manifest,
    commands: new Map(),
    exports: null,
    isActive: false,
    extensionUri: vscode.Uri.file(root),
  };

  const extensionRequire = createRequire(entry);
  const previous = activeExtension;
  activeExtension = extension;
  try {
    extension.exports = extensionRequire(entry);
    if (typeof extension.exports.activate === 'function') {
      const context = {
        extensionPath: root,
        extensionUri: extension.extensionUri,
        subscriptions: [],
        environmentVariableCollection: new Map(),
        asAbsolutePath(relativePath) { return join(root, relativePath); },
        storageUri: undefined,
        globalStorageUri: undefined,
      };
      extension.context = context;
      await extension.exports.activate(context);
    }
    extension.isActive = true;
  } finally {
    activeExtension = previous;
  }

  extensions.set(id, extension);
  vscode.extensions.all = [...extensions.values()];
  return {
    id,
    name: manifest.displayName ?? manifest.name,
    version: manifest.version,
    commands: [...extension.commands.keys()],
    contributes: manifest.contributes ?? {},
  };
}

async function executeCommand(command, args = []) {
  for (const extension of extensions.values()) {
    const callback = extension.commands.get(command);
    if (callback) return await callback(...args);
  }
  throw new Error(`Command not found: ${command}`);
}

async function handle(request) {
  switch (request.type) {
    case 'hello':
      response(request, { protocol: 2, runtime: 'node', electron: false, lsp: true });
      return;
    case 'load': {
      const extension = await loadExtension(request.extensionPath);
      response(request, { extension });
      for (const command of extension.contributes.commands ?? []) {
        send({ type: 'event', event: 'contribution', kind: 'command', command: command.command, title: command.title });
      }
      return;
    }
    case 'executeCommand':
      response(request, { result: await executeCommand(request.command, request.args ?? []) });
      return;
    case 'listExtensions':
      response(request, { extensions: [...extensions.values()].map(({ id, packageJSON, isActive }) => ({
        id, version: packageJSON.version, isActive,
      })) });
      return;
    case 'startLanguageServer':
      response(request, { languageServer: startLanguageServer(request.command, request.args ?? [], request.cwd ?? process.cwd()) });
      return;
    case 'languageServerRequest':
      sendLanguageServerMessage(request.message);
      response(request, { sent: true });
      return;
    case 'stopLanguageServer':
      response(request, { stopped: stopLanguageServer() });
      return;
    case 'shutdown':
      stopLanguageServer();
      response(request, { shuttingDown: true });
      process.exit(0);
      return;
    default:
      throw new Error(`Unknown message: ${request.type}`);
  }
}

await loadConfiguration();
const input = createInterface({ input: process.stdin, crlfDelay: Infinity });
input.on('line', async (line) => {
  if (!line.trim()) return;
  let request;
  try {
    request = JSON.parse(line);
    await handle(request);
  } catch (error) {
    failure(request ?? { id: null }, error);
  }
});

send({
  type: 'ready',
  protocol: 2,
  runtime: 'node',
  electron: false,
  api: ['commands', 'window', 'workspace', 'languages', 'extensions', 'Uri'],
  capabilities: ['configuration', 'documents', 'lsp-process-manager'],
});
