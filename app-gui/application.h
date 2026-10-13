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
#include <vector>

namespace GUI {

/**
 * @class   Application
 * @brief   Owns the SDL window, the SDL GPU device and the ImGui layer, and runs the main loop.
 * @details Call Init() once, then Run() if it succeeded, and always finish with Shutdown(). Init() returns
 *          Core::ExitFailure when SDL, the window, the GPU device or the ImGui layer cannot be set up, and keeps
 *          the reason for Shutdown() to report on stderr and in a message box. Dear ImGui renders through the
 *          SDL GPU API: Direct3D 12 on Windows, Vulkan on Linux.
 */
class Application {
public:
    Application() = default;
    ~Application();

    int Init();
    int Run();
    void Shutdown();

private:
    // Errors
    void AddError(const std::string &message);
    void DumpErrors();

    // Initialization
    int InitializeSDL();
    int CreateWindow();
    int CreateGPUDevice();
    void LoadAndSetIcon();
    int InitImGui();

    // Frame
    void PollEvents();
    void NewFrame();
    void UpdateWindowTitle();
    void EndFrame();

    SDL_Window *m_SDLWindow = nullptr;
    SDL_GPUDevice *m_GPUDevice = nullptr;
    float m_WindowScale = 1.0f;
    bool m_IsOpen = true;
    std::vector<std::string> m_Errors;

    // imgui.ini in the user's data folder; ImGui keeps only the pointer, so the path lives here
    std::string m_SettingsPath;
    // Last title given to the window, so it is only set again when the document or its state changes
    std::string m_WindowTitle;

    // Declared last so it is destroyed before the window and the ImGui context it draws with
    WindowManager m_WindowManager;
};

} // namespace GUI

#endif // SIGGEN_APPLICATION_H
