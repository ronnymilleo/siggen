/**
 * @file    imgui_layer.h
 * @brief   Dear ImGui and ImPlot setup, theme and per-frame calls for the GLFW/OpenGL 3 backends.
 */

#ifndef SIGGEN_IMGUI_LAYER_H
#define SIGGEN_IMGUI_LAYER_H

#include <GLFW/glfw3.h>

namespace GUI {

/**
 * @class   ImGuiLayer
 * @brief   Static wrapper around the process-wide ImGui and ImPlot contexts and their GLFW/OpenGL 3 backends.
 */
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer() = default;

    static void Init(GLFWwindow *window);
    static void Terminate();

    static void NewFrame();
    static void Render();
};

} // namespace GUI

#endif // SIGGEN_IMGUI_LAYER_H
