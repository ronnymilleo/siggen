/**
 * @file    theme.h
 * @brief   Visual theme of the interface: ImGui and ImPlot styles, colors and fonts.
 */

#ifndef SIGGEN_THEME_H
#define SIGGEN_THEME_H

#include "imgui.h"
#include <cstddef>
#include <span>

namespace GUI {

/**
 * @struct  ThemePalette
 * @brief   Colors of one theme: the interface surfaces, the accent and the plot grid.
 * @details Surfaces go from Background, the darkest on a dark theme, through Surface and Raised to RaisedHot, the
 *          hovered state. The accent marks the active and primary items; AccentDim fills them, Accent and AccentHot
 *          are their hovered and pressed states. The I/Q trace colors are shared by every theme.
 */
struct ThemePalette {
    const char *Name;
    bool Light;

    ImVec4 Background;
    ImVec4 Surface;
    ImVec4 Raised;
    ImVec4 RaisedHot;
    ImVec4 Border;
    ImVec4 Text;
    ImVec4 TextMuted;
    ImVec4 Accent;
    ImVec4 AccentHot;
    ImVec4 AccentDim;
    ImVec4 ErrorText;
    ImVec4 WarningText;
    ImVec4 PlotGrid;
};

// Setup
void ApplyTheme();
void LoadThemeFonts();
void RegisterThemeSettingsHandler();

// Themes
std::span<const ThemePalette> GetThemes();
std::size_t GetThemeIndex();
void SetTheme(std::size_t index);
const ThemePalette &GetPalette();

// Shared colors
ImVec4 GetThemeBackground();
ImVec4 GetErrorTextColor();
ImVec4 GetWarningTextColor();

// Widgets
bool PrimaryButton(const char *label, ImVec2 size = ImVec2(0.0f, 0.0f));

} // namespace GUI

#endif // SIGGEN_THEME_H
