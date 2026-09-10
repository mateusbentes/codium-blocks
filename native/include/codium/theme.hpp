// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#pragma once

#include <wx/colour.h>
#include <wx/string.h>

namespace codium {

enum class ThemeKind {
    System,
    Light,
    Dark,
    HighContrast,
};

struct ThemePalette final {
    wxColour window;
    wxColour panel;
    wxColour editor;
    wxColour editorText;
    wxColour editorMutedText;
    wxColour selection;
    wxColour accent;
    wxColour border;
    wxColour error;
    wxColour warning;
    wxColour information;
    wxColour hint;
    wxColour breakpointVerified;
    wxColour breakpointPending;
    wxColour breakpointRejected;
    bool dark = false;
    bool highContrast = false;

    static ThemePalette For(ThemeKind kind);
    static ThemeKind FromName(const wxString& name);
    static wxString Name(ThemeKind kind);
};

} // namespace codium
