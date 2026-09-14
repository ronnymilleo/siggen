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

/***********************************************************************************************************************
 * METHOD DEFINITIONS
 **********************************************************************************************************************/

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
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
#ifdef _WIN32
    io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\segoeui.ttf)", 18.0f);
#endif
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

    ImVec4 clear_color = ImVec4(0.2f, 0.2f, 0.2f, 1.00f);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w,
                 clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

/***********************************************************************************************************************
 * END OF FILE
 **********************************************************************************************************************/
