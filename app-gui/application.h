/**
 * @file    application.h
 * @brief   Native GLFW window with its OpenGL context and the ImGui layer bound to it.
 */

#ifndef SIGGEN_APPLICATION_H
#define SIGGEN_APPLICATION_H

#include <GLFW/glfw3.h>
#include <string>

namespace GUI {

/**
 * @class   Application
 * @brief   Owns the GLFW window, its OpenGL 3.3 core context and the ImGui context for the lifetime of the GUI.
 * @details Construction only validates the window size; Init() creates the window and the contexts, and the
 *          destructor releases whatever Init() managed to create.
 */
class Application {
public:
    Application(GLint window_width, GLint window_height);
    ~Application();

    void Init();
    bool GetShouldClose() const;
    void SwapBuffers();
    void ShowErrorMessage(const std::string &title, const std::string &message);

private:
    void ValidateWindowDimensions(GLint width, GLint height);
    void InitializeGLFW();
    void LoadAndSetIcon();
    void CreateWindow();
    void SetupOpenGL();

    GLFWwindow *m_GLFWWindow = nullptr;
    GLint m_Width = 800;
    GLint m_Height = 600;
    GLint m_BufferWidth = 0;
    GLint m_BufferHeight = 0;
    bool m_Initialized = false;
    std::string m_IconPath = "assets/icon.png";

    static constexpr GLint MinWindowWidth = 400;
    static constexpr GLint MaxWindowWidth = 3840;
    static constexpr GLint MinWindowHeight = 300;
    static constexpr GLint MaxWindowHeight = 2160;
};

} // namespace GUI

#endif // SIGGEN_APPLICATION_H
