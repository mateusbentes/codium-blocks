// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#include "codium/theme.hpp"

#include <wx/settings.h>

namespace codium {

namespace {

ThemePalette LightPalette()
{
    ThemePalette palette;
    palette.window = wxColour(246, 247, 249);
    palette.panel = wxColour(235, 238, 243);
    palette.editor = wxColour(255, 255, 255);
    palette.editorText = wxColour(28, 31, 36);
    palette.editorMutedText = wxColour(91, 99, 110);
    palette.selection = wxColour(190, 215, 248);
    palette.accent = wxColour(38, 104, 188);
    palette.border = wxColour(180, 188, 198);
    palette.error = wxColour(180, 35, 45);
    palette.warning = wxColour(145, 87, 0);
    palette.information = wxColour(24, 82, 150);
    palette.hint = wxColour(83, 91, 115);
    palette.breakpointVerified = wxColour(28, 86, 180);
    palette.breakpointPending = wxColour(150, 91, 0);
    palette.breakpointRejected = wxColour(180, 35, 45);
    palette.dark = false;
    palette.highContrast = false;
    return palette;
}

ThemePalette DarkPalette()
{
    ThemePalette palette;
    palette.window = wxColour(30, 33, 38);
    palette.panel = wxColour(38, 42, 49);
    palette.editor = wxColour(24, 27, 32);
    palette.editorText = wxColour(229, 231, 235);
    palette.editorMutedText = wxColour(164, 171, 182);
    palette.selection = wxColour(48, 91, 148);
    palette.accent = wxColour(111, 177, 255);
    palette.border = wxColour(82, 90, 102);
    palette.error = wxColour(255, 123, 132);
    palette.warning = wxColour(255, 205, 96);
    palette.information = wxColour(126, 191, 255);
    palette.hint = wxColour(184, 190, 213);
    palette.breakpointVerified = wxColour(113, 176, 255);
    palette.breakpointPending = wxColour(255, 201, 94);
    palette.breakpointRejected = wxColour(255, 123, 132);
    palette.dark = true;
    palette.highContrast = false;
    return palette;
}

ThemePalette HighContrastPalette()
{
    ThemePalette palette;
    palette.window = wxColour(0, 0, 0);
    palette.panel = wxColour(0, 0, 0);
    palette.editor = wxColour(0, 0, 0);
    palette.editorText = wxColour(255, 255, 255);
    palette.editorMutedText = wxColour(230, 230, 230);
    palette.selection = wxColour(255, 255, 0);
    palette.accent = wxColour(0, 255, 255);
    palette.border = wxColour(255, 255, 255);
    palette.error = wxColour(255, 80, 80);
    palette.warning = wxColour(255, 220, 0);
    palette.information = wxColour(0, 255, 255);
    palette.hint = wxColour(255, 255, 255);
    palette.breakpointVerified = wxColour(0, 255, 255);
    palette.breakpointPending = wxColour(255, 220, 0);
    palette.breakpointRejected = wxColour(255, 80, 80);
    palette.dark = true;
    palette.highContrast = true;
    return palette;
}

bool IsDarkSystemColour(const wxColour& colour)
{
    const int luminance = 299 * colour.Red() + 587 * colour.Green() + 114 * colour.Blue();
    return luminance < 128000;
}

} // namespace

ThemePalette ThemePalette::For(ThemeKind kind)
{
    if (kind == ThemeKind::Light) return LightPalette();
    if (kind == ThemeKind::Dark) return DarkPalette();
    if (kind == ThemeKind::HighContrast) return HighContrastPalette();

    const wxColour systemWindow = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    return IsDarkSystemColour(systemWindow) ? DarkPalette() : LightPalette();
}

ThemeKind ThemePalette::FromName(const wxString& name)
{
    const wxString normalized = name.Lower();
    if (normalized == wxS("light")) return ThemeKind::Light;
    if (normalized == wxS("dark")) return ThemeKind::Dark;
    if (normalized == wxS("high-contrast") || normalized == wxS("highcontrast")) {
        return ThemeKind::HighContrast;
    }
    return ThemeKind::System;
}

wxString ThemePalette::Name(ThemeKind kind)
{
    switch (kind) {
    case ThemeKind::Light: return wxS("Light");
    case ThemeKind::Dark: return wxS("Dark");
    case ThemeKind::HighContrast: return wxS("High contrast");
    case ThemeKind::System: return wxS("System");
    }
    return wxS("System");
}

} // namespace codium
