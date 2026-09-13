#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

import { readFileSync } from 'node:fs';

const payloadPath = process.argv[2];
if (!payloadPath) process.exit(2);
const payload = readFileSync(payloadPath);
process.stdout.write(payload);
process.stdin.resume();
process.stdin.on('error', () => process.exit(0));
