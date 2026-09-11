#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors


import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const [lsp, dap] = await Promise.all([
  readFile(new URL('./real-lsp-matrix.json', import.meta.url), 'utf8').then(JSON.parse),
  readFile(new URL('./real-dap-matrix.json', import.meta.url), 'utf8').then(JSON.parse),
]);

function platformValue(value, platform) {
  return typeof value === 'string' ? value : value?.[platform];
}

assert.equal(lsp.schemaVersion, 1);
assert.equal(dap.schemaVersion, 1);
assert.match(lsp.matrixVersion, /^20\d\d\.\d\d$/);
assert.match(dap.matrixVersion, /^20\d\d\.\d\d$/);
assert.deepEqual(lsp.servers.map((server) => server.id), ['clangd', 'rust-analyzer', 'gopls', 'pyright']);
assert.deepEqual(dap.adapters.map((adapter) => adapter.id), ['gdb-dap', 'lldb-dap', 'open-debug-ad7']);

for (const server of lsp.servers) {
  assert.ok(Array.isArray(server.versionArgs) && server.versionArgs.length > 0);
  assert.ok(server.command.linux || server.command.darwin || server.command.win32);
  for (const platform of ['linux', 'darwin', 'win32']) {
    if (!server.command[platform]) continue;
    assert.match(server.versionCommand?.[platform] ?? server.command[platform], /\S+/);
    assert.match(platformValue(server.version, platform), /\S+/);
    assert.match(platformValue(server.versionPattern, platform), /\S+/);
  }
  assert.match(server.languageId, /^\w+$/);
  assert.match(server.file, /\S+/);
  assert.match(server.text, /\n$/);
  assert.ok(Number.isInteger(server.position?.line) && server.position.line >= 0);
  assert.ok(Number.isInteger(server.position?.character) && server.position.character >= 0);
  assert.deepEqual(server.scenarios, [
    'hover', 'completion', 'definition', 'declaration', 'references',
    'documentSymbol', 'workspaceSymbol', 'rename',
  ]);
  assert.ok(Array.isArray(server.files));
  if (server.noViewsPolicy !== undefined) {
    for (const [platform, policy] of Object.entries(server.noViewsPolicy)) {
      assert.ok(['linux', 'darwin', 'win32'].includes(platform));
      assert.equal(policy, 'environment-skip');
    }
  }
}
for (const adapter of dap.adapters) {
  assert.ok(adapter.platforms.length > 0);
  assert.ok(adapter.platforms.every((platform) => ['linux', 'darwin', 'win32'].includes(platform)));
  assert.ok(Array.isArray(adapter.args));
  if (adapter.versionArgs === null) {
    assert.equal(adapter.versionPattern, null);
  } else {
    assert.ok(Array.isArray(adapter.versionArgs) && adapter.versionArgs.length > 0);
    for (const platform of adapter.platforms) {
      assert.match(adapter.versionCommand?.[platform] ?? adapter.command[platform], /\S+/);
      assert.match(platformValue(adapter.version, platform), /\S+/);
      assert.match(platformValue(adapter.versionPattern, platform), /\S+/);
    }
  }
  assert.match(adapter.scenario, /^(initialize-only|launch-stop-stack-breakpoints-disconnect)$/);
  assert.ok(adapter.requiresInitializedEvent === undefined || typeof adapter.requiresInitializedEvent === 'boolean');
  if (adapter.scenario === 'launch-stop-stack-breakpoints-disconnect') {
    assert.equal(adapter.debuggee, 'real-dap-debuggee.cpp');
    assert.ok(Number.isInteger(adapter.breakpointLine) && adapter.breakpointLine > 0);
  } else {
    assert.equal(adapter.debuggee, null);
    assert.equal(adapter.breakpointLine, null);
  }
  for (const platform of ['linux', 'darwin', 'win32']) {
    if (adapter.platforms.includes(platform)) assert.ok(adapter.command[platform]);
  }
}

console.log(`integration-matrix-smoke: ok — ${lsp.servers.length} LSP servers and ${dap.adapters.length} DAP adapters, schema v1`);
