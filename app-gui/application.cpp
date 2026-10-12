/**
 * @file    application.cpp
 * @brief   Top-level GUI application: owns the window, the ImGui layer, the generator session and the main loop.
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
 * @brief   Validates the fixed window size; the window itself is created by Init().
 * @note    Throws std::invalid_argument when the size is out of range.
 */
Application::Application() {
    ValidateWindowDimensions(m_Width, m_Height);
}

/**
 * @brief   Releases the ImGui layer, the window and GLFW, tolerating a partial Init().
 */
Application::~Application() {
    Shutdown();
}

/**
 * @brief   Creates the window, the OpenGL context, the window icon and the ImGui layer.
 * @note    Throws std::runtime_error when GLFW, the window or OpenGL fail to initialize.
 */
void Application::Init() {
    InitializeGLFW();
    CreateWindow();
    SetupOpenGL();
    LoadAndSetIcon();

    ImGuiLayer::Init(m_GLFWWindow);
    m_Initialized = true;
}

/**
 * @brief   Runs the frame loop until the user closes the window.
 * @return  0 when the window is closed normally.
 * @note    Errors raised while drawing a frame are logged and the loop continues; initialization errors propagate
 *          from Init().
 */
int Application::Run() {
    spdlog::debug("Entering main loop");
    while (!GetShouldClose()) {
        try {
            glfwPollEvents();
            ImGuiLayer::NewFrame();
            m_WindowManager.Render();
            ImGuiLayer::Render();
            SwapBuffers();
        } catch (const std::exception &error) {
            // Keep running so the user can see the error and carry on
            ShowErrorMessage("Runtime Error", error.what());
        }
    }
    spdlog::info("Application closing");
    return 0;
}

/**
 * @brief   Releases every resource created by Init(), tolerating a partial Init().
 * @note    Safe to call more than once.
 */
void Application::Shutdown() {
    if (m_Initialized) {
        ImGuiLayer::Terminate();
        m_Initialized = false;
    }
    if (m_GLFWWindow) {
        glfwDestroyWindow(m_GLFWWindow);
        m_GLFWWindow = nullptr;
    }
    glfwTerminate();
    spdlog::debug("Application resources released");
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

bool Application::GetShouldClose() const {
    return m_GLFWWindow ? glfwWindowShouldClose(m_GLFWWindow) : true;
}

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

} // namespace GUI
