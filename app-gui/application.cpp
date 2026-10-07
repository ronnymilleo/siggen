/**
 * @file    application.cpp
 * @brief   Native GLFW window with its OpenGL context and the ImGui layer bound to it.
 */

#include "application.h"

#include "imgui_layer.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <stdexcept>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace GUI {

/**
 * @brief   Records the requested window size; the window itself is created by Init().
 * @param[in] window_width   Window width in screen coordinates, from MinWindowWidth to MaxWindowWidth.
 * @param[in] window_height  Window height in screen coordinates, from MinWindowHeight to MaxWindowHeight.
 * @note    Throws std::invalid_argument when the size is out of range.
 */
Application::Application(GLint window_width, GLint window_height) : m_Width(window_width), m_Height(window_height) {
    try {
        ValidateWindowDimensions(window_width, window_height);
    } catch (const std::exception &error) {
        spdlog::error("Invalid application configuration: {}", error.what());
        throw;
    }
}

/**
 * @brief   Shuts down ImGui (if Init() reached it), destroys the window and terminates GLFW.
 */
Application::~Application() {
    try {
        if (m_Initialized) {
            ImGuiLayer::Terminate();
        }
        if (m_GLFWWindow) {
            glfwDestroyWindow(m_GLFWWindow);
        }
        glfwTerminate();
        spdlog::debug("Application resources released");
    } catch (const std::exception &error) {
        spdlog::error("Application cleanup failed: {}", error.what());
    }
}

/**
 * @brief   Creates the window, the OpenGL context, the window icon and the ImGui layer.
 * @note    Throws std::runtime_error when GLFW, the window or OpenGL fail to initialize.
 */
void Application::Init() {
    try {
        InitializeGLFW();
        CreateWindow();
        SetupOpenGL();
        LoadAndSetIcon();

        ImGuiLayer::Init(m_GLFWWindow);
        m_Initialized = true;
    } catch (const std::exception &error) {
        spdlog::error("Application initialization failed: {}", error.what());
        throw;
    }
}

/**
 * @brief   Tells whether the main loop should stop.
 * @return  True once the user closes the window, or when there is no window.
 */
bool Application::GetShouldClose() const {
    return m_GLFWWindow ? glfwWindowShouldClose(m_GLFWWindow) : true;
}

/**
 * @brief   Presents the frame just rendered.
 */
void Application::SwapBuffers() {
    if (m_GLFWWindow) {
        glfwSwapBuffers(m_GLFWWindow);
    }
}

/**
 * @brief   Reports an error raised while running a frame.
 * @param[in] title    Short context, such as "Runtime Error".
 * @param[in] message  The error text.
 */
void Application::ShowErrorMessage(const std::string &title, const std::string &message) {
    spdlog::error("{}: {}", title, message);
}

void Application::ValidateWindowDimensions(GLint width, GLint height) {
    if (width < MinWindowWidth || width > MaxWindowWidth || height < MinWindowHeight || height > MaxWindowHeight) {
        throw std::invalid_argument("Window size must be within " + std::to_string(MinWindowWidth) + "x" +
                                    std::to_string(MinWindowHeight) + " and " + std::to_string(MaxWindowWidth) + "x" +
                                    std::to_string(MaxWindowHeight));
    }
}

void Application::InitializeGLFW() {
    // Registered before initialization so startup failures include GLFW's diagnostic
    glfwSetErrorCallback([](int error, const char *description) {
        spdlog::error("GLFW error ({}): {}", error, description ? description : "Unknown error");
    });
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 8);
}

/**
 * @brief   Sets the window icon from m_IconPath, relative to the working directory.
 * @note    A missing icon only logs a warning.
 */
void Application::LoadAndSetIcon() {
    if (m_IconPath.empty()) {
        return;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    constexpr int RGBAChannels = 4;
    unsigned char *pixels = stbi_load(m_IconPath.c_str(), &width, &height, &channels, RGBAChannels);
    if (!pixels) {
        spdlog::warn("Unable to load window icon: {}", m_IconPath);
        return;
    }

    GLFWimage icon;
    icon.width = width;
    icon.height = height;
    icon.pixels = pixels;
    glfwSetWindowIcon(m_GLFWWindow, 1, &icon);
    stbi_image_free(pixels);
}

void Application::CreateWindow() {
    m_GLFWWindow = glfwCreateWindow(m_Width, m_Height, "Siggen", nullptr, nullptr);
    if (!m_GLFWWindow) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwGetFramebufferSize(m_GLFWWindow, &m_BufferWidth, &m_BufferHeight);
    if (m_BufferWidth <= 0 || m_BufferHeight <= 0) {
        throw std::runtime_error("Invalid framebuffer dimensions");
    }

    glfwMakeContextCurrent(m_GLFWWindow);
    glfwSwapInterval(1); // vsync
    glfwSetInputMode(m_GLFWWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetWindowUserPointer(m_GLFWWindow, this);
}

void Application::SetupOpenGL() {
    glEnable(GL_DEPTH_TEST);
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::stringstream message;
        message << "OpenGL error during setup: " << error;
        throw std::runtime_error(message.str());
    }

    glViewport(0, 0, m_BufferWidth, m_BufferHeight);
    error = glGetError();
    if (error != GL_NO_ERROR) {
        std::stringstream message;
        message << "OpenGL error setting viewport: " << error;
        throw std::runtime_error(message.str());
    }
}

} // namespace GUI
