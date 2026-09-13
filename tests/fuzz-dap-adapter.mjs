#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

import { readFileSync } from 'node:fs';

const payloadPath = process.argv[2];
if (!payloadPath) process.exit(2);
const payload = readFileSync(payloadPath);
// This is a finite parser fixture. Closing stdout after the payload lets the
// native client observe natural process exit instead of accumulating live
// Node children that must be force-terminated on Windows.
process.stdout.end(payload, () => process.exit(0));
