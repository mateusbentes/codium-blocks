#!/usr/bin/env node

process.stdin.setEncoding('utf8');
let buffer = '';

function emit(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

function handle(line) {
  if (!line.trim()) return;
  const request = JSON.parse(line);
  if (request.type === 'handshake') {
    const incompatible = process.argv.includes('--incompatible');
    emit({
      type: 'ready',
      contractMajor: incompatible ? 2 : 1,
      contractMinor: 0,
      sdkMajor: request.sdkMajor || 1,
      sdkMinor: request.sdkMinor || 36,
      sdkRelease: request.sdkRelease || 0,
      capabilities: ['sdkEventSink', 'projectEvents', 'projectTargets', 'compilerEvents', 'buildEvents', 'compilerDiagnostics', 'debuggerPluginMatched', 'debuggerEvents', 'debuggerControl']
    });
  } else if (request.type === 'openProject') {
    emit({
      type: 'event', event: 'projectOpened',
      projectPath: request.projectFile, message: 'Project opened by fake adapter'
    });
    emit({
      type: 'event', event: 'projectTarget',
      projectPath: request.projectFile, target: 'app', compilerId: 'gcc',
      outputPath: 'bin/app', workingDirectory: 'bin',
      message: 'Project target enumerated by fake adapter'
    });
  } else if (request.type === 'build') {
    emit({
      type: 'event', event: 'buildStarted',
      projectPath: request.projectFile, target: request.target, message: 'Build started'
    });
    emit({
      type: 'event', event: 'compilerDiagnostic',
      projectPath: request.projectFile, target: request.target,
      filePath: 'src/main.cpp', line: 12, column: 4,
      message: 'fake warning', isError: false
    });
    emit({
      type: 'event', event: 'buildFinished',
      projectPath: request.projectFile, target: request.target,
      exitCode: 0, message: 'Build finished'
    });
  } else if (request.type === 'debug') {
    emit({
      type: 'event', event: 'debugSessionStarted',
      projectPath: request.projectFile, target: request.target,
      plugin: 'Debugger', message: 'Debug session started'
    });
    if (request.breakOnEntry) {
      emit({
        type: 'event', event: 'debugSessionPaused',
        projectPath: request.projectFile, target: request.target,
        plugin: 'Debugger', message: 'Debug session paused at entry'
      });
    }
  } else if (request.type === 'continueDebug') {
    emit({ type: 'event', event: 'debugSessionContinued', plugin: 'Debugger', message: 'Debug session continued' });
  } else if (request.type === 'pauseDebug') {
    emit({ type: 'event', event: 'debugSessionPaused', plugin: 'Debugger', message: 'Debug session paused' });
  } else if (request.type === 'stopDebug') {
    emit({ type: 'event', event: 'debugSessionStopped', plugin: 'Debugger', exitCode: 0, message: 'Debug session stopped' });
  } else if (request.type === 'shutdown') {
    process.exit(0);
  }
}

process.stdin.on('data', (chunk) => {
  buffer += chunk;
  let newline = buffer.indexOf('\n');
  while (newline >= 0) {
    const line = buffer.slice(0, newline);
    buffer = buffer.slice(newline + 1);
    handle(line);
    newline = buffer.indexOf('\n');
  }
});
