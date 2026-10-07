/**
 * @file    imgui_layer.cpp
 * @brief   Dear ImGui and ImPlot setup, theme and per-frame calls for the GLFW/OpenGL 3 backends.
 */

#include "imgui_layer.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <filesystem>

namespace GUI {

namespace {

constexpr ImVec4 MakeColor(int red, int green, int blue, float alpha = 1.f) {
    return ImVec4(static_cast<float>(red) / 255.f, static_cast<float>(green) / 255.f, static_cast<float>(blue) / 255.f,
                  alpha);
}

// Deep slate surfaces with a single blue accent; I/Q trace colors match the plot palette
constexpr ImVec4 BackgroundColor = MakeColor(19, 22, 28);
constexpr ImVec4 SurfaceColor = MakeColor(27, 31, 39);
constexpr ImVec4 RaisedColor = MakeColor(38, 44, 55);
constexpr ImVec4 RaisedHotColor = MakeColor(48, 56, 70);
constexpr ImVec4 BorderColor = MakeColor(52, 59, 72);
constexpr ImVec4 TextColor = MakeColor(224, 228, 235);
constexpr ImVec4 TextMutedColor = MakeColor(139, 148, 163);
constexpr ImVec4 AccentColor = MakeColor(62, 142, 245);
constexpr ImVec4 AccentHotColor = MakeColor(94, 164, 250);
constexpr ImVec4 AccentDimColor = MakeColor(45, 104, 184);

void ApplyTheme() {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(16, 14);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 9);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.ScrollbarSize = 13.f;
    style.GrabMinSize = 12.f;
    style.WindowRounding = 0.f;
    style.ChildRounding = 6.f;
    style.FrameRounding = 5.f;
    style.PopupRounding = 6.f;
    style.ScrollbarRounding = 8.f;
    style.GrabRounding = 5.f;
    style.TabRounding = 5.f;
    style.WindowBorderSize = 0.f;
    style.FrameBorderSize = 0.f;
    style.PopupBorderSize = 1.f;
    style.SeparatorTextBorderSize = 1.f;
    style.SeparatorTextPadding = ImVec2(18, 6);

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = TextColor;
    colors[ImGuiCol_TextDisabled] = TextMutedColor;
    colors[ImGuiCol_WindowBg] = BackgroundColor;
    colors[ImGuiCol_ChildBg] = SurfaceColor;
    colors[ImGuiCol_PopupBg] = SurfaceColor;
    colors[ImGuiCol_Border] = BorderColor;
    colors[ImGuiCol_FrameBg] = RaisedColor;
    colors[ImGuiCol_FrameBgHovered] = RaisedHotColor;
    colors[ImGuiCol_FrameBgActive] = RaisedHotColor;
    colors[ImGuiCol_TitleBg] = SurfaceColor;
    colors[ImGuiCol_TitleBgActive] = SurfaceColor;
    colors[ImGuiCol_ScrollbarBg] = BackgroundColor;
    colors[ImGuiCol_ScrollbarGrab] = RaisedColor;
    colors[ImGuiCol_ScrollbarGrabHovered] = RaisedHotColor;
    colors[ImGuiCol_ScrollbarGrabActive] = AccentDimColor;
    colors[ImGuiCol_CheckMark] = AccentHotColor;
    colors[ImGuiCol_SliderGrab] = AccentColor;
    colors[ImGuiCol_SliderGrabActive] = AccentHotColor;
    colors[ImGuiCol_Button] = AccentDimColor;
    colors[ImGuiCol_ButtonHovered] = AccentColor;
    colors[ImGuiCol_ButtonActive] = AccentHotColor;
    colors[ImGuiCol_Header] = RaisedColor;
    colors[ImGuiCol_HeaderHovered] = RaisedHotColor;
    colors[ImGuiCol_HeaderActive] = AccentDimColor;
    colors[ImGuiCol_Separator] = BorderColor;
    colors[ImGuiCol_SeparatorHovered] = AccentColor;
    colors[ImGuiCol_SeparatorActive] = AccentHotColor;
    colors[ImGuiCol_Tab] = SurfaceColor;
    colors[ImGuiCol_TabHovered] = AccentColor;
    colors[ImGuiCol_TabSelected] = AccentDimColor;
    colors[ImGuiCol_TabDimmed] = SurfaceColor;
    colors[ImGuiCol_TabDimmedSelected] = RaisedColor;
    colors[ImGuiCol_TextSelectedBg] = MakeColor(62, 142, 245, 0.35f);
    colors[ImGuiCol_NavCursor] = AccentHotColor;
    colors[ImGuiCol_ModalWindowDimBg] = MakeColor(0, 0, 0, 0.55f);

    ImPlotStyle &plot_style = ImPlot::GetStyle();
    plot_style.PlotPadding = ImVec2(12, 12);
    plot_style.PlotBorderSize = 1.f;
    plot_style.LineWeight = 1.6f;
    plot_style.FitPadding = ImVec2(.15f, .15f);
    plot_style.Colors[ImPlotCol_PlotBg] = BackgroundColor;
    plot_style.Colors[ImPlotCol_PlotBorder] = BorderColor;
    plot_style.Colors[ImPlotCol_FrameBg] = SurfaceColor;
    plot_style.Colors[ImPlotCol_LegendBg] = MakeColor(27, 31, 39, 0.9f);
    plot_style.Colors[ImPlotCol_LegendBorder] = BorderColor;
    plot_style.Colors[ImPlotCol_AxisGrid] = MakeColor(255, 255, 255, 0.08f);
    plot_style.Colors[ImPlotCol_AxisText] = TextMutedColor;
    plot_style.Colors[ImPlotCol_AxisBg] = ImVec4(0, 0, 0, 0);
    plot_style.Colors[ImPlotCol_InlayText] = TextMutedColor;
}

} // namespace

/**
 * @brief   Creates the ImGui and ImPlot contexts, applies the theme and binds the GLFW and OpenGL 3 backends.
 * @param[in] window  The window whose OpenGL context is current.
 * @note    Loads the first system UI font found; falls back to the built-in one when none is installed.
 */
void ImGuiLayer::Init(GLFWwindow *window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ApplyTheme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    constexpr const char *FontCandidates[] = {
#ifdef __APPLE__
        "/System/Library/Fonts/Helvetica.ttc",
#else
        "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
#endif
    };
    for (const char *path : FontCandidates) {
        if (std::filesystem::exists(path) && io.Fonts->AddFontFromFileTTF(path, 17.0f)) {
            break;
        }
    }
}

/**
 * @brief   Shuts down the backends and destroys the ImGui and ImPlot contexts.
 */
void ImGuiLayer::Terminate() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();
}

/**
 * @brief   Starts a frame; every ImGui call of the frame goes between NewFrame() and Render().
 */
void ImGuiLayer::NewFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

/**
 * @brief   Clears the framebuffer with the theme background and draws the frame.
 */
void ImGuiLayer::Render() {
    ImGui::Render();

    const ImVec4 clear_color = BackgroundColor;
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w,
                 clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace GUI
