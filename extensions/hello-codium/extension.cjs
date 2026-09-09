const vscode = require('vscode');

function activate(context) {
  const output = vscode.window.createOutputChannel('Hello Codium::Blocks');
  const command = vscode.commands.registerCommand('hello.codium', async () => {
    output.appendLine('hello.codium command executed.');
    await vscode.window.showInformationMessage('Hello! This extension is running without Electron.');
    return 'hello-from-extension';
  });

  context.subscriptions.push(command);
  output.appendLine(`Activated in ${vscode.env.appName} (${vscode.env.appHost}).`);
}

function deactivate() {}

module.exports = { activate, deactivate };
