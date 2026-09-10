#!/usr/bin/env node

import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const [lsp, dap] = await Promise.all([
  readFile(new URL('./real-lsp-matrix.json', import.meta.url), 'utf8').then(JSON.parse),
  readFile(new URL('./real-dap-matrix.json', import.meta.url), 'utf8').then(JSON.parse),
]);

assert.equal(lsp.schemaVersion, 1);
assert.equal(dap.schemaVersion, 1);
assert.match(lsp.matrixVersion, /^20\d\d\.\d\d$/);
assert.match(dap.matrixVersion, /^20\d\d\.\d\d$/);
assert.deepEqual(lsp.servers.map((server) => server.id), ['clangd', 'rust-analyzer', 'gopls', 'pyright']);
assert.deepEqual(dap.adapters.map((adapter) => adapter.id), ['gdb-dap', 'lldb-dap', 'open-debug-ad7']);

for (const server of lsp.servers) {
  assert.match(server.version, /\S+/);
  assert.ok(server.command.linux || server.command.darwin || server.command.win32);
  assert.match(server.languageId, /^\w+$/);
  assert.match(server.file, /\S+/);
  assert.match(server.text, /\n$/);
  assert.ok(Array.isArray(server.files));
}
for (const adapter of dap.adapters) {
  assert.match(adapter.version, /\S+/);
  assert.ok(adapter.platforms.length > 0);
  assert.ok(adapter.platforms.every((platform) => ['linux', 'darwin', 'win32'].includes(platform)));
  assert.ok(Array.isArray(adapter.args));
  for (const platform of ['linux', 'darwin', 'win32']) {
    if (adapter.platforms.includes(platform)) assert.ok(adapter.command[platform]);
  }
}

console.log(`integration-matrix-smoke: ok — ${lsp.servers.length} LSP servers and ${dap.adapters.length} DAP adapters, schema v1`);
