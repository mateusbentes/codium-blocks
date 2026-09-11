// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

const vscode = require('vscode');

function activate(context) {
  const output = vscode.window.createOutputChannel('Hello Codium::Blocks');
  const settings = vscode.workspace.getConfiguration('helloCodium');
  const greeting = settings.get('greeting', 'Hello from Codium::Blocks');

  const helloCommand = vscode.commands.registerCommand('hello.codium', async () => {
    output.appendLine('hello.codium command executed.');
    await vscode.window.showInformationMessage(`${greeting} — this extension is running without Electron.`);
    return 'hello-from-extension';
  });

  const configureCommand = vscode.commands.registerCommand('hello.codium.configure', async () => {
    await settings.update('greeting', 'Configuration updated in Codium::Blocks');
    output.appendLine('hello.codium.configure command executed.');
    return 'configuration-updated';
  });

  const tree = vscode.window.registerTreeDataProvider('hello.codium.views', {
    getChildren() {
      return [new vscode.TreeItem(`Greeting: ${greeting}`), new vscode.TreeItem('Electron-free host')];
    },
  });
  const onOpen = vscode.workspace.onDidOpenTextDocument((document) => {
    output.appendLine(`Opened document: ${document.fileName}`);
  });
  const onChange = vscode.workspace.onDidChangeTextDocument((document) => {
    output.appendLine(`Changed document: ${document.fileName} v${document.version}`);
  });
  const onSave = vscode.workspace.onDidSaveTextDocument((document) => {
    output.appendLine(`Saved document: ${document.fileName}`);
  });

  context.subscriptions.push(helloCommand, configureCommand, tree, onOpen, onChange, onSave);
  output.appendLine(`Activated in ${vscode.env.appName} (${vscode.env.appHost}).`);
  output.appendLine(`Configured greeting: ${greeting}`);
}

function deactivate() {}

module.exports = { activate, deactivate };
