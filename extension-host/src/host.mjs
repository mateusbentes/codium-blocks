#!/usr/bin/env node

import { createRequire } from 'node:module';
import { createInterface } from 'node:readline';
import { readFile } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import Module from 'node:module';

const originalLoad = Module._load;
const extensions = new Map();
let activeExtension = null;

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

const vscode = {
  version: '0.1.0-codium-blocks',
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
        get(_key, defaultValue) { return defaultValue; },
        has() { return false; },
        inspect() { return undefined; },
        async update() {},
        section,
      };
    },
    onDidChangeConfiguration() { return disposable(() => {}); },
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
    module: null,
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
  return { id, name: manifest.displayName ?? manifest.name, version: manifest.version, commands: [...extension.commands.keys()] };
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
      response(request, { protocol: 1, runtime: 'node', electron: false });
      return;
    case 'load':
      response(request, { extension: await loadExtension(request.extensionPath) });
      return;
    case 'executeCommand':
      response(request, { result: await executeCommand(request.command, request.args ?? []) });
      return;
    case 'listExtensions':
      response(request, { extensions: [...extensions.values()].map(({ id, packageJSON, isActive }) => ({
        id, version: packageJSON.version, isActive,
      })) });
      return;
    case 'shutdown':
      response(request, { shuttingDown: true });
      process.exit(0);
      return;
    default:
      throw new Error(`Unknown message: ${request.type}`);
  }
}

const input = createInterface({ input: process.stdin, crlfDelay: Infinity });
input.on('line', async (line) => {
  if (!line.trim()) return;
  try {
    const request = JSON.parse(line);
    await handle(request);
  } catch (error) {
    try {
      failure(JSON.parse(line), error);
    } catch {
      send({ type: 'response', id: null, ok: false, error: 'Invalid JSON request.' });
    }
  }
});

send({ type: 'ready', protocol: 1, runtime: 'node', electron: false, api: ['commands', 'window', 'workspace', 'languages', 'extensions', 'Uri'] });
