#!/usr/bin/env node

process.stdin.setEncoding('utf8');
let buffer = '';
process.stdin.resume();
process.stdout.write('ready\n');
process.stdin.on('data', (chunk) => {
  buffer += chunk;
  while (true) {
    const newline = buffer.indexOf('\n');
    if (newline < 0) break;
    const line = buffer.slice(0, newline).replace(/\r$/, '');
    buffer = buffer.slice(newline + 1);
    if (line === 'ping') process.stdout.write('pong\n');
    else if (line === 'ansi') process.stdout.write('\u001b[31mred\u001b[0m\n');
    else if (line === 'stderr') process.stderr.write('fake-terminal-error\n');
    else if (line === 'exit') process.exit(0);
    else process.stdout.write(`echo:${line}\n`);
  }
});
