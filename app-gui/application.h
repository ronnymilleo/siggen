/**
 * @file    application.h
 * @brief   Top-level GUI application: owns the window, the ImGui layer, the generator session and the main loop.
 */

#ifndef SIGGEN_APPLICATION_H
#define SIGGEN_APPLICATION_H

#include "window_manager.h"
#include <GLFW/glfw3.h>
#include <string>

namespace GUI {

/**
 * @class   Application
 * @brief   Owns the GLFW window, its OpenGL 3.3 core context, the ImGui layer and the generator windows, and runs
 *          the main loop.
 * @details Call Init() once, then Run() if it succeeded, and always finish with Shutdown(). Init() throws when the
 *          window or the graphics context cannot be created; errors raised while drawing a frame are logged and the
 *          loop continues.
 */
class Application {
public:
    Application();
    ~Application();

    void Init();
    int Run();
    void Shutdown();

private:
    void ValidateWindowDimensions(GLint width, GLint height);
    void InitializeGLFW();
    void LoadAndSetIcon();
    void CreateWindow();
    void SetupOpenGL();
    bool GetShouldClose() const;
    void SwapBuffers();
    void ShowErrorMessage(const std::string &title, const std::string &message);

    GLFWwindow *m_GLFWWindow = nullptr;
    GLint m_Width = 1280;
    GLint m_Height = 800;
    GLint m_BufferWidth = 0;
    GLint m_BufferHeight = 0;
    bool m_Initialized = false;
    std::string m_IconPath = "assets/icon.png";

    // Declared last so it is destroyed before the window and the ImGui context it draws with
    WindowManager m_WindowManager;

    static constexpr GLint MinWindowWidth = 400;
    static constexpr GLint MaxWindowWidth = 3840;
    static constexpr GLint MinWindowHeight = 300;
    static constexpr GLint MaxWindowHeight = 2160;
};

} // namespace GUI

#endif // SIGGEN_APPLICATION_H
