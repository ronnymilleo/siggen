/**
 * @file    imgui_layer.h
 * @brief   Dear ImGui and ImPlot setup, theme and per-frame calls for the SDL3 and SDL GPU backends.
 */

#ifndef SIGGEN_IMGUI_LAYER_H
#define SIGGEN_IMGUI_LAYER_H

#include "imgui.h"
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

namespace GUI {

/**
 * @class   ImGuiLayer
 * @brief   Static wrapper around the process-wide ImGui and ImPlot contexts and their SDL3/SDL GPU backends.
 */
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer() = default;

    static void Init(SDL_Window *window, SDL_GPUDevice *device, float scale);
    static void Terminate();

    static void NewFrame();
    static void Render();

    static const ImVec4 &BackgroundColor();
};

} // namespace GUI

#endif // SIGGEN_IMGUI_LAYER_H
