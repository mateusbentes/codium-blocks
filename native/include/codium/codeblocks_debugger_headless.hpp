// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#pragma once

class cbDebugInterfaceFactory;
class cbDebuggerMenuHandler;

namespace codium {

// These objects satisfy the Code::Blocks debugger SDK UI dependencies without
// exposing Code::Blocks windows or menus to the Codium::Blocks process.
cbDebugInterfaceFactory* CreateHeadlessDebuggerInterfaceFactory();
cbDebuggerMenuHandler* CreateHeadlessDebuggerMenuHandler();

} // namespace codium
