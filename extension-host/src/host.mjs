#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors


import { createRequire } from 'node:module';
import { createInterface } from 'node:readline';
import { mkdir, readFile, rename, writeFile } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import { dirname, isAbsolute, join, relative, resolve } from 'node:path';
import { homedir } from 'node:os';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { once } from 'node:events';
import Module from 'node:module';

const originalLoad = Module._load;
const extensions = new Map();
const configurationListeners = new Set();
const configuration = new Map();
const documentListeners = {
  open: new Set(),
  change: new Set(),
  save: new Set(),
};
const documents = new Map();
let activeExtension = null;
let languageServer = null;
const languageRequestMethods = new Map();

const defaultDataRoot = process.platform === 'win32'
  ? join(process.env.APPDATA || homedir(), 'CodiumBlocks')
  : process.platform === 'darwin'
    ? join(homedir(), 'Library', 'Application Support', 'CodiumBlocks')
    : join(process.env.XDG_STATE_HOME || join(homedir(), '.local', 'state'), 'codium-blocks');
const dataRoot = process.env.CODIUM_BLOCKS_DATA ?? defaultDataRoot;
const configurationFile = join(dataRoot, 'settings.json');
const extensionHostRoot = dirname(dirname(fileURLToPath(import.meta.url)));
const extensionRoots = [
  join(dirname(extensionHostRoot), 'extensions'),
  join(extensionHostRoot, 'extensions'),
  join(dataRoot, 'extensions'),
  ...(process.env.CODIUM_BLOCKS_EXTENSION_ROOTS ?? '')
    .split(process.platform === 'win32' ? ';' : ':')
    .filter(Boolean),
].map((root) => resolve(root));
const maximumLanguageServerHeader = 64 * 1024;
const maximumLanguageServerFrame = 16 * 1024 * 1024;
const maximumBrokerLine = 4 * 1024 * 1024;

function isPathWithin(candidate, parent) {
  const child = resolve(candidate);
  const root = resolve(parent);
  const suffix = relative(root, child);
  return suffix === '' || (!suffix.startsWith('..') && !isAbsolute(suffix));
}

function isAllowedExtensionRoot(extensionPath) {
  return extensionRoots.some((root) => isPathWithin(extensionPath, root));
}

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
  const temporaryFile = `${configurationFile}.tmp-${process.pid}`;
  await writeFile(temporaryFile, `${JSON.stringify(Object.fromEntries(configuration), null, 2)}\n`, 'utf8');
  await rename(temporaryFile, configurationFile);
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

function makeDocument(value) {
  const text = String(value.text ?? '');
  return Object.freeze({
    uri: { toString: () => String(value.uri ?? ''), fsPath: String(value.uri ?? '') },
    fileName: String(value.uri ?? ''),
    languageId: String(value.languageId ?? 'plaintext'),
    version: Number(value.version ?? 1),
    isDirty: false,
    getText: () => text,
    lineAt: (line) => String(text.split(/\r?\n/)[line] ?? ''),
  });
}

class TreeItem {
  constructor(label, collapsibleState = 0) {
    this.label = String(label);
    this.collapsibleState = collapsibleState;
    this.id = undefined;
    this.command = undefined;
    this.tooltip = undefined;
  }
}

async function publishTreeView(extension, viewId, provider, element) {
  const children = await provider.getChildren(element);
  const items = (children ?? []).map((child) => {
    const item = child instanceof TreeItem ? child : new TreeItem(child);
    return {
      label: item.label,
      id: item.id,
      tooltip: item.tooltip,
      collapsibleState: item.collapsibleState,
      command: item.command,
    };
  });
  send({ type: 'event', event: 'treeView', extension: extension.id, viewId, items });
  return items;
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
    registerTreeDataProvider(viewId, provider) {
      const extension = currentExtension();
      if (!provider || typeof provider.getChildren !== 'function') {
        throw new Error(`Tree data provider for ${viewId} must implement getChildren().`);
      }
      extension.treeProviders.set(viewId, provider);
      void publishTreeView(extension, viewId, provider).catch((error) => {
        send({ type: 'event', event: 'treeViewError', extension: extension.id, viewId, message: error.message });
      });
      return disposable(() => extension.treeProviders.delete(viewId));
    },
    createTreeView(viewId, options = {}) {
      const provider = options.treeDataProvider;
      if (!provider) throw new Error(`Tree view ${viewId} requires a treeDataProvider.`);
      const disposableRegistration = this.registerTreeDataProvider(viewId, provider);
      return { id: viewId, onDidChangeVisibility: () => disposable(() => {}), dispose: disposableRegistration.dispose };
    },
  },
  workspace: {
    workspaceFolders: [],
    textDocuments: [],
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
    onDidOpenTextDocument(listener) {
      documentListeners.open.add(listener);
      return disposable(() => documentListeners.open.delete(listener));
    },
    onDidChangeTextDocument(listener) {
      documentListeners.change.add(listener);
      return disposable(() => documentListeners.change.delete(listener));
    },
    onDidSaveTextDocument(listener) {
      documentListeners.save.add(listener);
      return disposable(() => documentListeners.save.delete(listener));
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
  TreeItem,
  TreeItemCollapsibleState: { None: 0, Collapsed: 1, Expanded: 2 },
};

// Compatibility bridge for CommonJS VS Code extensions. A future version can
// replace this with a capability-scoped loader and a worker pool.
Module._load = function patchedLoad(request, parent, isMain) {
  if (request === 'vscode') return vscode;
  return originalLoad.call(this, request, parent, isMain);
};

function parseLanguageServerFrames(state, chunk) {
  if (state.buffer.length + chunk.length > maximumLanguageServerFrame + maximumLanguageServerHeader) {
    state.protocolError = 'Language server input exceeded the maximum buffered frame size.';
    state.child.kill();
    return;
  }
  state.buffer = Buffer.concat([state.buffer, chunk]);
  while (true) {
    const separator = state.buffer.indexOf(Buffer.from('\r\n\r\n'));
    if (separator < 0) {
      if (state.buffer.length > maximumLanguageServerHeader) {
        state.protocolError = 'Language server header exceeded the maximum size.';
        state.child.kill();
      }
      return;
    }
    if (separator > maximumLanguageServerHeader) {
      state.protocolError = 'Language server header exceeded the maximum size.';
      state.child.kill();
      return;
    }
    const header = state.buffer.subarray(0, separator).toString('ascii');
    const match = header.match(/Content-Length:\s*(\d+)/i);
    if (!match) {
      state.protocolError = 'Language server frame did not contain Content-Length.';
      state.child.kill();
      return;
    }
    const bodyStart = separator + 4;
    const lengthText = match[1];
    const length = Number(lengthText);
    if (!Number.isSafeInteger(length) || length < 0 || length > maximumLanguageServerFrame) {
      state.protocolError = 'Language server Content-Length is outside the allowed range.';
      state.child.kill();
      return;
    }
    if (state.buffer.length < bodyStart + length) return;
    const body = state.buffer.subarray(bodyStart, bodyStart + length).toString('utf8');
    state.buffer = state.buffer.subarray(bodyStart + length);
    try {
      const message = JSON.parse(body);
      send({ type: 'event', event: 'languageServerMessage', message });
      if (message.method === 'textDocument/publishDiagnostics') {
        send({
          type: 'event',
          event: 'diagnostics',
          uri: message.params?.uri,
          diagnostics: message.params?.diagnostics ?? [],
        });
      }
      if (message.id !== undefined && languageRequestMethods.has(message.id)) {
        const method = languageRequestMethods.get(message.id);
        languageRequestMethods.delete(message.id);
        send({ type: 'event', event: 'languageServerResult', method, result: message.result ?? null, error: message.error ?? null });
        if (method === 'initialize' && !message.error) {
          sendLanguageServerMessage({ jsonrpc: '2.0', method: 'initialized', params: {} });
        }
      }
    } catch (error) {
      send({ type: 'event', event: 'languageServerError', message: `Invalid JSON from language server: ${error.message}` });
      state.child.kill();
    }
  }
}

function sendLanguageServerMessage(message) {
  if (!languageServer?.child?.stdin?.writable) {
    throw new Error('No language server is running.');
  }
  const body = JSON.stringify(message);
  if (message.id !== undefined && message.method) {
    languageRequestMethods.set(message.id, message.method);
  }
  const header = `Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n`;
  languageServer.child.stdin.write(header + body);
}

async function stopLanguageServer() {
  if (!languageServer) return false;
  const state = languageServer;
  languageServer = null;
  if (state.child.exitCode === null && !state.child.killed) {
    state.child.kill();
    await Promise.race([once(state.child, 'exit'), new Promise((resolvePromise) => setTimeout(resolvePromise, 1000))]);
    if (state.child.exitCode === null) state.child.kill('SIGKILL');
  }
  return true;
}

async function handleWorkspaceDocumentEvent(request) {
  const document = makeDocument(request.document ?? {});
  const key = document.uri.toString();
  if (request.event === 'open' || request.event === 'change') documents.set(key, document);
  if (request.event === 'save' && !documents.has(key)) documents.set(key, document);
  if (request.event === 'save') documents.set(key, document);
  vscode.workspace.textDocuments = [...documents.values()];
  const listeners = documentListeners[request.event];
  if (!listeners) throw new Error(`Unknown workspace document event: ${request.event}`);
  for (const listener of listeners) {
    try {
      await listener(document);
    } catch (error) {
      send({ type: 'event', event: 'extensionError', message: error.message });
    }
  }
  send({ type: 'event', event: 'workspaceDocument', action: request.event, uri: key, version: document.version });
  return { action: request.event, uri: key, documents: vscode.workspace.textDocuments.length };
}

async function startLanguageServer(command, args = [], cwd = process.cwd()) {
  await stopLanguageServer();
  if (!command || typeof command !== 'string' || !cwd || typeof cwd !== 'string') {
    throw new Error('A language server command and working directory are required.');
  }
  const child = spawn(command, args, { cwd, stdio: ['pipe', 'pipe', 'pipe'] });
  const state = { child, buffer: Buffer.alloc(0), command };
  languageServer = state;
  child.stdout.on('data', (chunk) => parseLanguageServerFrames(state, chunk));
  child.stderr.on('data', (chunk) => send({ type: 'event', event: 'languageServerStderr', message: chunk.toString() }));
  child.on('error', (error) => send({ type: 'event', event: 'languageServerError', message: error.message }));
  child.on('exit', (code, signal) => {
    send({ type: 'event', event: 'languageServerExit', command, code, signal, protocolError: state.protocolError ?? null });
    if (languageServer === state) languageServer = null;
  });
  return { command, args, cwd };
}

async function loadExtension(extensionPath) {
  const root = resolve(extensionPath);
  if (!isAllowedExtensionRoot(root)) {
    throw new Error(`Extension path is outside the configured extension roots: ${root}`);
  }
  const manifest = JSON.parse(await readFile(join(root, 'package.json'), 'utf8'));
  if (!manifest.name || !manifest.publisher) {
    throw new Error('Invalid manifest: name and publisher are required.');
  }
  if (manifest.engines?.vscode && manifest.engines.vscode === '*') {
    throw new Error('Manifests with engines.vscode=* are not accepted by this host.');
  }
  const mainRelativePath = String(manifest.main ?? 'extension.js');
  const entry = resolve(root, mainRelativePath);
  if (isAbsolute(mainRelativePath) || !isPathWithin(entry, root)) {
    throw new Error('Extension manifest main must remain inside the extension directory.');
  }

  const id = `${manifest.publisher}.${manifest.name}`;
  const extension = {
    id,
    extensionPath: root,
    packageJSON: manifest,
    commands: new Map(),
    treeProviders: new Map(),
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
      for (const view of extension.contributes.views?.['codium-blocks'] ?? []) {
        send({ type: 'event', event: 'contribution', kind: 'view', viewId: view.id, title: view.name });
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
      response(request, { languageServer: await startLanguageServer(request.command, request.args ?? [], request.cwd ?? process.cwd()) });
      return;
    case 'languageServerRequest':
      sendLanguageServerMessage(request.message);
      response(request, { sent: true });
      return;
    case 'languageServerNotification':
      sendLanguageServerMessage(request.message);
      response(request, { sent: true });
      return;
    case 'stopLanguageServer':
      response(request, { stopped: await stopLanguageServer() });
      return;
    case 'workspaceDocumentEvent':
      response(request, await handleWorkspaceDocumentEvent(request));
      return;
    case 'shutdown':
      await stopLanguageServer();
      response(request, { shuttingDown: true });
      setImmediate(() => process.exit(0));
      return;
    default:
      throw new Error(`Unknown message: ${request.type}`);
  }
}

await loadConfiguration();
const input = createInterface({ input: process.stdin, crlfDelay: Infinity });
let requestQueue = Promise.resolve();
input.on('line', async (line) => {
  requestQueue = requestQueue.then(async () => {
    if (!line.trim()) return;
    let request;
    try {
      if (Buffer.byteLength(line, 'utf8') > maximumBrokerLine) {
        throw new Error('Broker message exceeds the maximum JSON Lines size.');
      }
      request = JSON.parse(line);
      await handle(request);
    } catch (error) {
      failure(request ?? { id: null }, error);
    }
  });
});

send({
  type: 'ready',
  protocol: 2,
  runtime: 'node',
  electron: false,
  api: ['commands', 'window', 'workspace', 'languages', 'extensions', 'Uri', 'TreeItem'],
  capabilities: ['configuration', 'documents', 'workspace-events', 'tree-views', 'lsp-process-manager'],
});
