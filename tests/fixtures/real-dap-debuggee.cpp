// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors

volatile int global_counter = 0;

static int helper(int value)
{
    global_counter = value;
    return value + 1;
}

int main()
{
    global_counter = helper(41);
    return global_counter == 42 ? 0 : 1;
}
