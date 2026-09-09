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

  context.subscriptions.push(helloCommand, configureCommand);
  output.appendLine(`Activated in ${vscode.env.appName} (${vscode.env.appHost}).`);
  output.appendLine(`Configured greeting: ${greeting}`);
}

function deactivate() {}

module.exports = { activate, deactivate };
