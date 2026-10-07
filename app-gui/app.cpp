/***********************************************************************************************************************
 *
 * @file app.cpp
 * @brief App class method definitions.
 *
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * DEPENDENCIES
 **********************************************************************************************************************/

#include "app.h"
#define STB_IMAGE_IMPLEMENTATION
#include "imgui_layer.h"
#include "stb_image.h"
#include <spdlog/spdlog.h>
#include <parameter_validator.h>
#include <sstream>

/***********************************************************************************************************************
 * METHOD DEFINITIONS
 **********************************************************************************************************************/

App::App()
{
    std::fill(std::begin(m_Keys), std::end(m_Keys), false);
}

App::App(GLint WindowWidth, GLint WindowHeight) : m_Width(WindowWidth), m_Height(WindowHeight)
{
    try
    {
        ValidateWindowDimensions(WindowWidth, WindowHeight);
        std::fill(std::begin(m_Keys), std::end(m_Keys), false);
    }
    catch (const std::exception& e)
    {
        spdlog::error("Invalid application configuration: {}", e.what());
        throw;
    }
}

App::~App()
{
    try
    {
        if (m_Initialized)
        {
            ImGuiLayer::Terminate();
        }
        if (m_GLFWWindow)
        {
            glfwDestroyWindow(m_GLFWWindow);
        }
        glfwTerminate();
        spdlog::debug("Application resources released");
    }
    catch (const std::exception& e)
    {
        spdlog::error("Application cleanup failed: {}", e.what());
    }
}

void App::Init()
{
    try
    {
        InitializeGLFW();
        CreateWindow();
        SetupOpenGL();
        LoadAndSetIcon();

        ImGuiLayer::Init(m_GLFWWindow);
        m_Initialized = true;
    }
    catch (const std::exception& e)
    {
        spdlog::error("Application initialization failed: {}", e.what());
        throw;
    }
}

void App::ValidateWindowDimensions(GLint width, GLint height)
{
    ParameterValidator::ValidateIntRange(width, MIN_WINDOW_WIDTH, MAX_WINDOW_WIDTH, "Window Width");
    ParameterValidator::ValidateIntRange(height, MIN_WINDOW_HEIGHT, MAX_WINDOW_HEIGHT, "Window Height");
}

void App::InitializeGLFW()
{
    // Register before initialization so startup failures include GLFW's diagnostic.
    glfwSetErrorCallback([](int error, const char* description) {
        spdlog::error("GLFW error ({}): {}", error, description ? description : "Unknown error");
    });
    if (!glfwInit())
    {
        throw GraphicsException("Failed to initialize GLFW");
    }

    // Setup GLFW Windows Properties (OpenGL Version)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 8);
}

void App::LoadAndSetIcon()
{
    if (m_IconPath.empty())
    {
        return;
    }

    // Load icon using stb_image
    int            width, height, channels;
    unsigned char* pixels = stbi_load(m_IconPath.c_str(), &width, &height, &channels, 4); // Force RGBA

    if (!pixels)
    {
        spdlog::warn("Unable to load window icon: {}", m_IconPath);
        return;
    }

    // Create GLFW image structure
    GLFWimage icon;
    icon.width  = width;
    icon.height = height;
    icon.pixels = pixels;

    // Set the window icon
    glfwSetWindowIcon(m_GLFWWindow, 1, &icon);

    // Free the loaded image data
    stbi_image_free(pixels);
}

void App::CreateWindow()
{
    m_GLFWWindow = glfwCreateWindow(m_Width, m_Height, "Siggen", nullptr, nullptr);
    if (!m_GLFWWindow)
    {
        glfwTerminate();
        throw GraphicsException("Failed to create GLFW window");
    }

    // Get buffer size information
    glfwGetFramebufferSize(m_GLFWWindow, &m_BufferWidth, &m_BufferHeight);

    if (m_BufferWidth <= 0 || m_BufferHeight <= 0)
    {
        throw GraphicsException("Invalid framebuffer dimensions");
    }

    // Set the current context
    glfwMakeContextCurrent(m_GLFWWindow);
    glfwSwapInterval(1); // Enable vsync

    // Handle inputs
    glfwSetInputMode(m_GLFWWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetWindowUserPointer(m_GLFWWindow, this);
}

void App::SetupOpenGL()
{
    // Enable depth testing
    glEnable(GL_DEPTH_TEST);

    // Check for OpenGL errors
    GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        std::stringstream ss;
        ss << "OpenGL error during setup: " << error;
        throw GraphicsException(ss.str());
    }

    // Create Viewport
    glViewport(0, 0, m_BufferWidth, m_BufferHeight);

    // Check for viewport errors
    error = glGetError();
    if (error != GL_NO_ERROR)
    {
        std::stringstream ss;
        ss << "OpenGL error setting viewport: " << error;
        throw GraphicsException(ss.str());
    }
}

void App::SwapBuffers()
{
    if (m_GLFWWindow)
    {
        glfwSwapBuffers(m_GLFWWindow);
    }
}

void App::ShowErrorMessage(const std::string& title, const std::string& message)
{
    spdlog::error("{}: {}", title, message);
    // Could be extended to show platform-specific error dialogs
}

/***********************************************************************************************************************
 * END OF FILE
 **********************************************************************************************************************/
