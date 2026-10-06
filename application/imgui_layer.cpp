/***********************************************************************************************************************
 *
 * @file ImGuiLayer.cpp
 * @brief ImGuiLayer class method definitions.
 *
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * DEPENDENCIES
 **********************************************************************************************************************/

#include "imgui_layer.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include <filesystem>

/***********************************************************************************************************************
 * METHOD DEFINITIONS
 **********************************************************************************************************************/

namespace
{
constexpr ImVec4 rgb(int r, int g, int b, float a = 1.f)
{
    return ImVec4(static_cast<float>(r) / 255.f, static_cast<float>(g) / 255.f, static_cast<float>(b) / 255.f, a);
}

// Deep slate surfaces with a single blue accent; I/Q trace colors match the plot palette.
constexpr ImVec4 kBackground = rgb(19, 22, 28);
constexpr ImVec4 kSurface    = rgb(27, 31, 39);
constexpr ImVec4 kRaised     = rgb(38, 44, 55);
constexpr ImVec4 kRaisedHot  = rgb(48, 56, 70);
constexpr ImVec4 kBorder     = rgb(52, 59, 72);
constexpr ImVec4 kText       = rgb(224, 228, 235);
constexpr ImVec4 kTextMuted  = rgb(139, 148, 163);
constexpr ImVec4 kAccent     = rgb(62, 142, 245);
constexpr ImVec4 kAccentHot  = rgb(94, 164, 250);
constexpr ImVec4 kAccentDim  = rgb(45, 104, 184);

void ApplyTheme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding     = ImVec2(16, 14);
    s.FramePadding      = ImVec2(10, 6);
    s.ItemSpacing       = ImVec2(10, 9);
    s.ItemInnerSpacing  = ImVec2(8, 6);
    s.ScrollbarSize     = 13.f;
    s.GrabMinSize       = 12.f;
    s.WindowRounding    = 0.f;
    s.ChildRounding     = 6.f;
    s.FrameRounding     = 5.f;
    s.PopupRounding     = 6.f;
    s.ScrollbarRounding = 8.f;
    s.GrabRounding      = 5.f;
    s.TabRounding       = 5.f;
    s.WindowBorderSize  = 0.f;
    s.FrameBorderSize   = 0.f;
    s.PopupBorderSize   = 1.f;
    s.SeparatorTextBorderSize = 1.f;
    s.SeparatorTextPadding    = ImVec2(18, 6);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = kText;
    c[ImGuiCol_TextDisabled]         = kTextMuted;
    c[ImGuiCol_WindowBg]             = kBackground;
    c[ImGuiCol_ChildBg]              = kSurface;
    c[ImGuiCol_PopupBg]              = kSurface;
    c[ImGuiCol_Border]               = kBorder;
    c[ImGuiCol_FrameBg]              = kRaised;
    c[ImGuiCol_FrameBgHovered]       = kRaisedHot;
    c[ImGuiCol_FrameBgActive]        = kRaisedHot;
    c[ImGuiCol_TitleBg]              = kSurface;
    c[ImGuiCol_TitleBgActive]        = kSurface;
    c[ImGuiCol_ScrollbarBg]          = kBackground;
    c[ImGuiCol_ScrollbarGrab]        = kRaised;
    c[ImGuiCol_ScrollbarGrabHovered] = kRaisedHot;
    c[ImGuiCol_ScrollbarGrabActive]  = kAccentDim;
    c[ImGuiCol_CheckMark]            = kAccentHot;
    c[ImGuiCol_SliderGrab]           = kAccent;
    c[ImGuiCol_SliderGrabActive]     = kAccentHot;
    c[ImGuiCol_Button]               = kAccentDim;
    c[ImGuiCol_ButtonHovered]        = kAccent;
    c[ImGuiCol_ButtonActive]         = kAccentHot;
    c[ImGuiCol_Header]               = kRaised;
    c[ImGuiCol_HeaderHovered]        = kRaisedHot;
    c[ImGuiCol_HeaderActive]         = kAccentDim;
    c[ImGuiCol_Separator]            = kBorder;
    c[ImGuiCol_SeparatorHovered]     = kAccent;
    c[ImGuiCol_SeparatorActive]      = kAccentHot;
    c[ImGuiCol_Tab]                  = kSurface;
    c[ImGuiCol_TabHovered]           = kAccent;
    c[ImGuiCol_TabSelected]          = kAccentDim;
    c[ImGuiCol_TabDimmed]            = kSurface;
    c[ImGuiCol_TabDimmedSelected]    = kRaised;
    c[ImGuiCol_TextSelectedBg]       = rgb(62, 142, 245, 0.35f);
    c[ImGuiCol_NavCursor]            = kAccentHot;
    c[ImGuiCol_ModalWindowDimBg]     = rgb(0, 0, 0, 0.55f);

    ImPlotStyle& p = ImPlot::GetStyle();
    p.PlotPadding   = ImVec2(12, 12);
    p.PlotBorderSize = 1.f;
    p.LineWeight    = 1.6f;
    p.FitPadding    = ImVec2(.15f, .15f);
    p.Colors[ImPlotCol_PlotBg]     = kBackground;
    p.Colors[ImPlotCol_PlotBorder] = kBorder;
    p.Colors[ImPlotCol_FrameBg]    = kSurface;
    p.Colors[ImPlotCol_LegendBg]   = rgb(27, 31, 39, 0.9f);
    p.Colors[ImPlotCol_LegendBorder] = kBorder;
    p.Colors[ImPlotCol_AxisGrid]   = rgb(255, 255, 255, 0.08f);
    p.Colors[ImPlotCol_AxisText]   = kTextMuted;
    p.Colors[ImPlotCol_AxisBg]     = ImVec4(0, 0, 0, 0);
    p.Colors[ImPlotCol_InlayText]  = kTextMuted;
}
} // namespace

void ImGuiLayer::Init(GLFWwindow* window)
{
    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    ImGui::StyleColorsDark();
    ApplyTheme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    // Prefer a system UI font; fall back to the built-in one when none is installed.
    constexpr const char* font_candidates[] = {
#ifdef __APPLE__
        "/System/Library/Fonts/Helvetica.ttc",
#else
        "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
#endif
    };
    for (const char* path : font_candidates)
    {
        if (std::filesystem::exists(path) && io.Fonts->AddFontFromFileTTF(path, 17.0f))
            break;
    }
}

void ImGuiLayer::Terminate()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();
}

void ImGuiLayer::NewFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::Render()
{
    ImGui::Render();

    ImVec4 clear_color = kBackground;
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w,
                 clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

/***********************************************************************************************************************
 * END OF FILE
 **********************************************************************************************************************/
