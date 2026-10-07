/***********************************************************************************************************************
 *
 * @file app.h
 * @brief App class prototype.
 *
 **********************************************************************************************************************/

#pragma once

/***********************************************************************************************************************
 * DEPENDENCIES
 **********************************************************************************************************************/

#include <GLFW/glfw3.h>
#include <exceptions.h>

/***********************************************************************************************************************
 * MACROS AND TYPEDEFS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * CLASS
 **********************************************************************************************************************/

class App
{
public:
    App();
    App(GLint WindowWidth, GLint WindowHeight);
    ~App();

    void Init(); // Changed return type to void, throws exceptions on error
    bool GetShouldClose() const
    {
        return m_GLFWWindow ? glfwWindowShouldClose(m_GLFWWindow) : true;
    }
    void SwapBuffers();
    void ShowErrorMessage(const std::string& title, const std::string& message);

    GLFWwindow* GetGLFWwindow() const
    {
        return m_GLFWWindow;
    }

    bool* GetKeys()
    {
        return m_Keys;
    }

private:
    void ValidateWindowDimensions(GLint width, GLint height);
    void InitializeGLFW();
    void LoadAndSetIcon();
    void CreateWindow();
    void SetupOpenGL();

    GLFWwindow* m_GLFWWindow   = nullptr;
    GLint       m_Width        = 800;
    GLint       m_Height       = 600;
    GLint       m_BufferWidth  = 0;
    GLint       m_BufferHeight = 0;
    bool        m_Initialized  = false;

    bool m_Keys[1024]{};

    std::string m_IconPath = "assets/icon.png";

    static constexpr GLint MIN_WINDOW_WIDTH  = 400;
    static constexpr GLint MAX_WINDOW_WIDTH  = 3840;
    static constexpr GLint MIN_WINDOW_HEIGHT = 300;
    static constexpr GLint MAX_WINDOW_HEIGHT = 2160;
};

/***********************************************************************************************************************
 * END OF FILE
 **********************************************************************************************************************/
