// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Codium::Blocks Contributors
#include "codium/theme.hpp"

#include <wx/init.h>

#include <cstdlib>
#include <iostream>

namespace {

int Luminance(const wxColour& colour)
{
    return 299 * colour.Red() + 587 * colour.Green() + 114 * colour.Blue();
}

int Contrast(const wxColour& foreground, const wxColour& background)
{
    return std::abs(Luminance(foreground) - Luminance(background));
}

} // namespace

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::cerr << "theme-smoke: wxWidgets initialization failed\n";
        return 1;
    }

    const auto light = codium::ThemePalette::For(codium::ThemeKind::Light);
    const auto dark = codium::ThemePalette::For(codium::ThemeKind::Dark);
    const auto highContrast = codium::ThemePalette::For(codium::ThemeKind::HighContrast);
    if (light.dark || dark.highContrast || !highContrast.dark || !highContrast.highContrast) {
        std::cerr << "theme-smoke: theme flags failed\n";
        return 2;
    }
    if (Contrast(light.editorText, light.editor) < 50000 ||
        Contrast(dark.editorText, dark.editor) < 50000 ||
        Contrast(highContrast.editorText, highContrast.editor) < 100000) {
        std::cerr << "theme-smoke: editor contrast failed\n";
        return 3;
    }
    if (codium::ThemePalette::FromName(wxS("high-contrast")) != codium::ThemeKind::HighContrast ||
        codium::ThemePalette::FromName(wxS("unknown")) != codium::ThemeKind::System ||
        codium::ThemePalette::Name(codium::ThemeKind::Dark) != wxS("Dark")) {
        std::cerr << "theme-smoke: theme names failed\n";
        return 4;
    }

    std::cout << "theme-smoke: ok — light, dark, system, and high-contrast palettes\n";
    return 0;
}
