/**
 * @file    theme.cpp
 * @brief   Visual theme of the interface: ImGui and ImPlot styles, colors and fonts.
 */

#include "theme.h"

#include "font_files.h"
#include "imgui_internal.h"
#include "implot.h"
#include <algorithm>
#include <array>
#include <span>
#include <string_view>

namespace GUI {

namespace {

constexpr ImVec4 Rgb(const int red, const int green, const int blue, const float alpha = 1.0f) {
    return {static_cast<float>(red) / 255.0f, static_cast<float>(green) / 255.0f, static_cast<float>(blue) / 255.0f,
            alpha};
}

constexpr auto Themes = std::to_array<ThemePalette>({
    // Cool blue-gray surfaces with a blue accent; errors stay red, apart from the accent
    {.Name = "Graphite",
     .Light = false,
     .Background = Rgb(22, 25, 30),
     .Surface = Rgb(30, 34, 41),
     .Raised = Rgb(43, 49, 58),
     .RaisedHot = Rgb(56, 63, 74),
     .Border = Rgb(61, 68, 80),
     .Text = Rgb(225, 230, 237),
     .TextMuted = Rgb(139, 148, 160),
     .Accent = Rgb(56, 132, 244),
     .AccentHot = Rgb(96, 160, 255),
     .AccentDim = Rgb(36, 92, 176),
     .ErrorText = Rgb(255, 107, 94),
     .WarningText = Rgb(240, 190, 90),
     .PlotGrid = Rgb(255, 255, 255, 0.08f)},
    // Warm charcoal surfaces with a glowing ember-orange accent
    {.Name = "Ember",
     .Light = false,
     .Background = Rgb(21, 18, 19),
     .Surface = Rgb(29, 24, 26),
     .Raised = Rgb(42, 34, 37),
     .RaisedHot = Rgb(56, 44, 48),
     .Border = Rgb(64, 50, 54),
     .Text = Rgb(238, 230, 230),
     .TextMuted = Rgb(158, 140, 143),
     // Glowing ember orange-terracotta, clearly separated from red errors
     .Accent = Rgb(228, 86, 52),
     .AccentHot = Rgb(255, 115, 78),
     .AccentDim = Rgb(164, 52, 28),
     .ErrorText = Rgb(255, 75, 92),
     .WarningText = Rgb(255, 196, 64),
     .PlotGrid = Rgb(255, 255, 255, 0.08f)},
    // A green monochrome CRT, balanced for eye comfort: deep charcoal-emerald base, luminous P31 mint text,
    // and bright electric green for traces
    {.Name = "Phosphor",
     .Light = false,
     .Background = Rgb(11, 17, 13),
     .Surface = Rgb(16, 26, 19),
     .Raised = Rgb(24, 38, 28),
     .RaisedHot = Rgb(33, 53, 39),
     .Border = Rgb(38, 62, 45),
     .Text = Rgb(196, 240, 210),
     .TextMuted = Rgb(105, 145, 118),
     .Accent = Rgb(38, 166, 91),
     .AccentHot = Rgb(0, 230, 118),
     .AccentDim = Rgb(22, 102, 54),
     .ErrorText = Rgb(255, 110, 90),
     .WarningText = Rgb(255, 210, 80),
     .PlotGrid = Rgb(0, 230, 118, 0.10f)},
    // An analog oscilloscope screen: green phosphor signals on near-black, with a warm amber accent for controls
    {.Name = "Oscilloscope",
     .Light = false,
     .Background = Rgb(12, 16, 13),
     .Surface = Rgb(18, 24, 20),
     .Raised = Rgb(28, 38, 31),
     .RaisedHot = Rgb(40, 54, 44),
     .Border = Rgb(48, 64, 52),
     .Text = Rgb(210, 235, 215),
     .TextMuted = Rgb(120, 150, 128),
     .Accent = Rgb(220, 140, 25),
     .AccentHot = Rgb(255, 180, 50),
     .AccentDim = Rgb(150, 95, 18),
     .ErrorText = Rgb(255, 90, 75),
     .WarningText = Rgb(255, 200, 70),
     .PlotGrid = Rgb(0, 230, 118, 0.12f)},
    // Cool dark slate interface with a glowing neon phosphor accent
    {.Name = "Cyber Slate",
     .Light = false,
     .Background = Rgb(18, 20, 23),
     .Surface = Rgb(25, 29, 34),
     .Raised = Rgb(38, 44, 52),
     .RaisedHot = Rgb(50, 58, 68),
     .Border = Rgb(56, 64, 75),
     .Text = Rgb(225, 230, 236),
     .TextMuted = Rgb(135, 144, 155),
     .Accent = Rgb(0, 200, 100),
     .AccentHot = Rgb(35, 255, 135),
     .AccentDim = Rgb(0, 115, 60),
     .ErrorText = Rgb(255, 95, 80),
     .WarningText = Rgb(255, 205, 75),
     .PlotGrid = Rgb(0, 230, 118, 0.12f)},
    // Classic engineering blueprint: Prussian navy, crisp chalk-white text, cyan accent, drafting gold warnings
    {.Name = "Blueprint",
     .Light = false,
     .Background = Rgb(11, 21, 36),
     .Surface = Rgb(18, 33, 54),
     .Raised = Rgb(27, 47, 76),
     .RaisedHot = Rgb(38, 64, 102),
     .Border = Rgb(46, 76, 120),
     .Text = Rgb(225, 238, 255),
     .TextMuted = Rgb(130, 160, 200),
     .Accent = Rgb(56, 189, 248),
     .AccentHot = Rgb(125, 218, 255),
     .AccentDim = Rgb(24, 105, 175),
     .ErrorText = Rgb(255, 100, 90),
     .WarningText = Rgb(250, 204, 21),
     .PlotGrid = Rgb(56, 189, 248, 0.12f)},
    // Monochrome amber CRT / plasma monitor: zero blue light, rich charcoal with golden phosphor glow
    {.Name = "Amber",
     .Light = false,
     .Background = Rgb(18, 15, 13),
     .Surface = Rgb(26, 21, 17),
     .Raised = Rgb(38, 30, 24),
     .RaisedHot = Rgb(54, 42, 33),
     .Border = Rgb(64, 50, 38),
     .Text = Rgb(255, 222, 175),
     .TextMuted = Rgb(165, 135, 100),
     .Accent = Rgb(245, 158, 11),
     .AccentHot = Rgb(255, 195, 45),
     .AccentDim = Rgb(165, 95, 8),
     .ErrorText = Rgb(255, 85, 70),
     .WarningText = Rgb(255, 225, 100),
     .PlotGrid = Rgb(245, 158, 11, 0.12f)},
    // Off-white paper with dark ink and a blue accent, like a printed datasheet; the accent fills stay light so
    // dark text reads on them
    {.Name = "Paper",
     .Light = true,
     .Background = Rgb(246, 244, 239),
     .Surface = Rgb(236, 233, 226),
     .Raised = Rgb(222, 218, 209),
     .RaisedHot = Rgb(208, 203, 192),
     .Border = Rgb(196, 190, 178),
     .Text = Rgb(34, 34, 38),
     .TextMuted = Rgb(110, 106, 100),
     .Accent = Rgb(160, 192, 240),
     .AccentHot = Rgb(70, 130, 220),
     .AccentDim = Rgb(196, 215, 245),
     .ErrorText = Rgb(196, 40, 30),
     .WarningText = Rgb(160, 100, 0),
     .PlotGrid = Rgb(0, 0, 0, 0.08f)},
    // Aged parchment with sepia ink and a wax-red accent, like a mid-century vacuum-tube manual
    {.Name = "Vintage",
     .Light = true,
     .Background = Rgb(245, 240, 230),
     .Surface = Rgb(237, 230, 218),
     .Raised = Rgb(224, 215, 200),
     .RaisedHot = Rgb(210, 200, 182),
     .Border = Rgb(195, 182, 162),
     .Text = Rgb(46, 36, 28),
     .TextMuted = Rgb(120, 105, 92),
     .Accent = Rgb(225, 160, 140),
     .AccentHot = Rgb(185, 65, 45),
     .AccentDim = Rgb(240, 205, 190),
     .ErrorText = Rgb(185, 40, 30),
     .WarningText = Rgb(170, 95, 10),
     .PlotGrid = Rgb(46, 36, 28, 0.08f)},
});

// Section of imgui.ini that keeps the chosen theme
constexpr const char *SettingsTypeName = "Theme";

std::size_t current_theme = 0;

constexpr float FontSize = 17.0f;

// The atlas only reads the bytes, which live for the whole program
ImFont *AddBundledFont(const std::span<const unsigned char> bytes) {
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): ImGui takes a mutable pointer it does not write through
    void *data = const_cast<unsigned char *>(bytes.data());
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(data, static_cast<int>(bytes.size()), FontSize, &config);
}

void ApplyImGuiSizes() {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 12.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextPadding = ImVec2(18.0f, 6.0f);
}

// Colors only, so a theme can change while running without undoing the display scaling of the sizes
void ApplyImGuiColors(const ThemePalette &palette) {
    ImGuiStyle &style = ImGui::GetStyle();
    if (palette.Light) {
        ImGui::StyleColorsLight(&style);
    } else {
        ImGui::StyleColorsDark(&style);
    }
    const ImVec4 translucent_accent = {palette.Accent.x, palette.Accent.y, palette.Accent.z, 0.5f};
    const ImVec4 selected_text = {palette.Accent.x, palette.Accent.y, palette.Accent.z, 0.35f};
    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = palette.Text;
    colors[ImGuiCol_TextDisabled] = palette.TextMuted;
    colors[ImGuiCol_WindowBg] = palette.Background;
    colors[ImGuiCol_ChildBg] = palette.Surface;
    colors[ImGuiCol_PopupBg] = palette.Surface;
    colors[ImGuiCol_Border] = palette.Border;
    colors[ImGuiCol_FrameBg] = palette.Raised;
    colors[ImGuiCol_FrameBgHovered] = palette.RaisedHot;
    colors[ImGuiCol_FrameBgActive] = palette.RaisedHot;
    colors[ImGuiCol_TitleBg] = palette.Surface;
    colors[ImGuiCol_TitleBgActive] = palette.Surface;
    colors[ImGuiCol_TitleBgCollapsed] = palette.Surface;
    colors[ImGuiCol_MenuBarBg] = palette.Surface;
    colors[ImGuiCol_ScrollbarBg] = palette.Background;
    colors[ImGuiCol_ScrollbarGrab] = palette.Raised;
    colors[ImGuiCol_ScrollbarGrabHovered] = palette.RaisedHot;
    colors[ImGuiCol_ScrollbarGrabActive] = palette.AccentDim;
    colors[ImGuiCol_CheckMark] = palette.AccentHot;
    colors[ImGuiCol_SliderGrab] = palette.Accent;
    colors[ImGuiCol_SliderGrabActive] = palette.AccentHot;
    // Buttons stay neutral; PrimaryButton() fills the main actions with the accent
    colors[ImGuiCol_Button] = palette.Raised;
    colors[ImGuiCol_ButtonHovered] = palette.AccentDim;
    colors[ImGuiCol_ButtonActive] = palette.Accent;
    colors[ImGuiCol_Header] = palette.Raised;
    colors[ImGuiCol_HeaderHovered] = palette.RaisedHot;
    colors[ImGuiCol_HeaderActive] = palette.AccentDim;
    colors[ImGuiCol_Separator] = palette.Border;
    colors[ImGuiCol_SeparatorHovered] = palette.Accent;
    colors[ImGuiCol_SeparatorActive] = palette.AccentHot;
    colors[ImGuiCol_ResizeGrip] = palette.Raised;
    colors[ImGuiCol_ResizeGripHovered] = palette.Accent;
    colors[ImGuiCol_ResizeGripActive] = palette.AccentHot;
    colors[ImGuiCol_InputTextCursor] = palette.Text;
    colors[ImGuiCol_Tab] = palette.Surface;
    colors[ImGuiCol_TabHovered] = palette.Accent;
    colors[ImGuiCol_TabSelected] = palette.AccentDim;
    colors[ImGuiCol_TabSelectedOverline] = palette.AccentHot;
    colors[ImGuiCol_TabDimmed] = palette.Surface;
    colors[ImGuiCol_TabDimmedSelected] = palette.Raised;
    colors[ImGuiCol_TabDimmedSelectedOverline] = palette.Border;
    colors[ImGuiCol_DockingPreview] = translucent_accent;
    colors[ImGuiCol_DockingEmptyBg] = palette.Background;
    colors[ImGuiCol_TableHeaderBg] = palette.Raised;
    colors[ImGuiCol_TableBorderStrong] = palette.Border;
    colors[ImGuiCol_TableBorderLight] = palette.Raised;
    colors[ImGuiCol_TextLink] = palette.AccentHot;
    colors[ImGuiCol_TextSelectedBg] = selected_text;
    colors[ImGuiCol_DragDropTarget] = palette.AccentHot;
    colors[ImGuiCol_NavCursor] = palette.AccentHot;
    colors[ImGuiCol_ModalWindowDimBg] = Rgb(0, 0, 0, 0.55f);
}

void ApplyImPlotColors(const ThemePalette &palette) {
    ImPlotStyle &style = ImPlot::GetStyle();
    style.Colors[ImPlotCol_PlotBg] = palette.Background;
    style.Colors[ImPlotCol_PlotBorder] = palette.Border;
    style.Colors[ImPlotCol_FrameBg] = palette.Surface;
    style.Colors[ImPlotCol_LegendBg] = {palette.Surface.x, palette.Surface.y, palette.Surface.z, 0.9f};
    style.Colors[ImPlotCol_LegendBorder] = palette.Border;
    style.Colors[ImPlotCol_LegendText] = palette.Text;
    style.Colors[ImPlotCol_TitleText] = palette.Text;
    style.Colors[ImPlotCol_AxisGrid] = palette.PlotGrid;
    style.Colors[ImPlotCol_AxisText] = palette.TextMuted;
    style.Colors[ImPlotCol_AxisBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImPlotCol_InlayText] = palette.TextMuted;
    style.Colors[ImPlotCol_Crosshairs] = palette.Text;
}

void ApplyColors(const ThemePalette &palette) {
    ApplyImGuiColors(palette);
    ApplyImPlotColors(palette);
}

} // namespace

/**
 * @brief   Applies the spacing, rounding and the colors of the current theme to ImGui and ImPlot.
 * @note    Call after both contexts exist and before scaling the style for the display DPI.
 */
void ApplyTheme() {
    ApplyImGuiSizes();
    ImPlotStyle &plot_style = ImPlot::GetStyle();
    plot_style.PlotPadding = ImVec2(12.0f, 12.0f);
    plot_style.PlotBorderSize = 1.0f;
    plot_style.LineWeight = 1.6f;
    plot_style.FitPadding = ImVec2(0.15f, 0.15f);
    ApplyColors(GetPalette());
}

/**
 * @brief   Loads the bundled interface font, Inter; the atlas keeps the default glyphs as fallback.
 */
void LoadThemeFonts() {
    AddBundledFont(InterFont);
}

/**
 * @brief   Keeps the chosen theme in imgui.ini, by name, and applies it when the file is read.
 * @note    Call once after the ImGui context exists and before the first frame.
 */
void RegisterThemeSettingsHandler() {
    ImGuiSettingsHandler handler;
    handler.TypeName = SettingsTypeName;
    handler.TypeHash = ImHashStr(SettingsTypeName);
    handler.ReadOpenFn = [](ImGuiContext *, ImGuiSettingsHandler *, const char *) -> void * {
        // Any non-null pointer tells ImGui to pass the lines of the section to ReadLineFn
        return &current_theme;
    };
    handler.ReadLineFn = [](ImGuiContext *, ImGuiSettingsHandler *, void *, const char *line) {
        const std::string_view text = line;
        constexpr std::string_view NameKey = "Name=";
        if (!text.starts_with(NameKey)) {
            return;
        }
        const auto themes = GetThemes();
        const auto theme = std::ranges::find(themes, text.substr(NameKey.size()), &ThemePalette::Name);
        if (theme != themes.end()) {
            SetTheme(static_cast<std::size_t>(theme - themes.begin()));
        }
    };
    handler.WriteAllFn = [](ImGuiContext *, ImGuiSettingsHandler *, ImGuiTextBuffer *buffer) {
        buffer->appendf("[%s][Current]\n", SettingsTypeName);
        buffer->appendf("Name=%s\n\n", GetPalette().Name);
    };
    ImGui::AddSettingsHandler(&handler);
}

/**
 * @brief   Lists the themes the View menu offers.
 * @return  Every theme, the default first.
 */
std::span<const ThemePalette> GetThemes() {
    return Themes;
}

/**
 * @brief   Returns the position of the current theme in GetThemes().
 * @return  Index of the current theme.
 */
std::size_t GetThemeIndex() {
    return current_theme;
}

/**
 * @brief   Switches to another theme and applies its colors to ImGui and ImPlot right away.
 * @param[in] index  Position in GetThemes(); out of range indices are ignored.
 * @note    Sizes are left alone, so it can be called while running, after the style was scaled for the display.
 */
void SetTheme(const std::size_t index) {
    if (index >= Themes.size()) {
        return;
    }
    current_theme = index;
    ApplyColors(Themes[index]);
}

/**
 * @brief   Returns the colors of the current theme.
 * @return  The palette, valid for the whole program.
 */
const ThemePalette &GetPalette() {
    return Themes[current_theme];
}

/**
 * @brief   Returns the window background color, also used to clear the frame.
 * @return  Background color as RGBA.
 */
ImVec4 GetThemeBackground() {
    return GetPalette().Background;
}

/**
 * @brief   Returns the color of error messages.
 * @return  Error text color as RGBA.
 */
ImVec4 GetErrorTextColor() {
    return GetPalette().ErrorText;
}

/**
 * @brief   Returns the color of warning messages.
 * @return  Warning text color as RGBA.
 */
ImVec4 GetWarningTextColor() {
    return GetPalette().WarningText;
}

/**
 * @brief   Draws a button filled with the accent color, for the main action of a window or dialog.
 * @param[in] label  Button label, as in ImGui::Button().
 * @param[in] size   Button size, as in ImGui::Button(); zero fits the label.
 * @return  Whether the button was clicked.
 */
bool PrimaryButton(const char *label, const ImVec2 size) {
    const ThemePalette &palette = GetPalette();
    ImGui::PushStyleColor(ImGuiCol_Button, palette.AccentDim);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette.Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, palette.AccentHot);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

} // namespace GUI
