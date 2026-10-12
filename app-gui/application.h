/**
 * @file    application.h
 * @brief   Top-level GUI application: owns the window, the GPU device, the generator session and the main loop.
 */

#ifndef SIGGEN_APPLICATION_H
#define SIGGEN_APPLICATION_H

#include "window_manager.h"
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>
#include <string>

namespace GUI {

/**
 * @class   Application
 * @brief   Owns the SDL window, the SDL GPU device and the ImGui layer, and runs the main loop.
 * @details Call Init() once, then Run() if it succeeded, and always finish with Shutdown(). Init() throws when SDL,
 *          the GPU device or the ImGui layer cannot be set up; errors raised while drawing a frame are logged and
 *          the loop continues. Dear ImGui renders through the SDL GPU API: Direct3D 12 on Windows, Vulkan on Linux.
 */
class Application {
public:
    Application() = default;
    ~Application();

    void Init();
    int Run();
    void Shutdown();

private:
    void InitializeSDL();
    void CreateWindow();
    void CreateGPUDevice();
    void InitImGui();
    void LoadAndSetIcon();
    void PollEvents();
    void NewFrame();
    void EndFrame();
    void ShowErrorMessage(const std::string &title, const std::string &message);

    SDL_Window *m_SDLWindow = nullptr;
    SDL_GPUDevice *m_GPUDevice = nullptr;
    float m_WindowScale = 1.0f;
    bool m_IsOpen = true;

    // Declared last so it is destroyed before the window and the ImGui context it draws with
    WindowManager m_WindowManager;
};

} // namespace GUI

#endif // SIGGEN_APPLICATION_H
