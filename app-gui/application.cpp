/**
 * @file    application.cpp
 * @brief   Top-level GUI application: owns the window, the GPU device, the generator session and the main loop.
 */

#include "application.h"

#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "implot.h"
#include "theme.h"
#include <SDL3/SDL.h>
#include <format>
#include <spdlog/spdlog.h>
#include <stdexcept>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace GUI {

namespace {

// Debug builds ask for the validation layers of the GPU API; SDL runs without them when they are not installed
#ifdef NDEBUG
constexpr bool GPUDebugMode = false;
#else
constexpr bool GPUDebugMode = true;
#endif

} // namespace

/**
 * @brief   Releases the ImGui layer, the GPU device, the window and SDL, tolerating a partial Init().
 */
Application::~Application() {
    Shutdown();
}

/**
 * @brief   Initializes SDL, the window, the GPU device, the window icon and the ImGui layer, in that order.
 * @note    Throws std::runtime_error when SDL, the window or the GPU device cannot be set up.
 */
void Application::Init() {
    InitializeSDL();
    CreateWindow();
    CreateGPUDevice();
    LoadAndSetIcon();
    InitImGui();
}

/**
 * @brief   Runs the frame loop until the user closes the window.
 * @return  0 when the window is closed normally.
 * @note    Errors raised while drawing a frame are logged and the loop continues; initialization errors propagate
 *          from Init().
 */
int Application::Run() {
    spdlog::debug("Entering main loop");
    while (m_IsOpen) {
        PollEvents();

        if (SDL_GetWindowFlags(m_SDLWindow) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        try {
            NewFrame();
            m_WindowManager.Render();
            ImGui::Render();
            UpdateWindowTitle();
            EndFrame();
        } catch (const std::exception &error) {
            // Keep running so the user can see the error and carry on
            ShowErrorMessage("Runtime Error", error.what());
        }
        if (m_WindowManager.IsQuitConfirmed()) {
            m_IsOpen = false;
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
    if (m_GPUDevice != nullptr) {
        SDL_WaitForGPUIdle(m_GPUDevice);
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
    }
    if (m_GPUDevice != nullptr) {
        SDL_ReleaseWindowFromGPUDevice(m_GPUDevice, m_SDLWindow);
        SDL_DestroyGPUDevice(m_GPUDevice);
        m_GPUDevice = nullptr;
    }
    if (m_SDLWindow != nullptr) {
        SDL_DestroyWindow(m_SDLWindow);
        m_SDLWindow = nullptr;
    }
    SDL_Quit();
    spdlog::debug("Application resources released");
}

void Application::InitializeSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        throw std::runtime_error(std::string("SDL_Init(): ") + SDL_GetError());
    }
}

void Application::CreateWindow() {
    m_WindowScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    constexpr int BaseWidth = 1280;
    constexpr int BaseHeight = 800;
    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    m_SDLWindow = SDL_CreateWindow("Siggen", static_cast<int>(BaseWidth * m_WindowScale),
                                   static_cast<int>(BaseHeight * m_WindowScale), flags);
    if (m_SDLWindow == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateWindow(): ") + SDL_GetError());
    }
}

// SDL picks the native API of the system (Direct3D 12 on Windows, Vulkan on Linux) among the shader formats the
// Dear ImGui backend ships
void Application::CreateGPUDevice() {
    m_GPUDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                          SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB,
                                      GPUDebugMode, nullptr);
    if (m_GPUDevice == nullptr) {
        throw std::runtime_error(std::string("SDL_CreateGPUDevice(): ") + SDL_GetError());
    }
    if (!SDL_ClaimWindowForGPUDevice(m_GPUDevice, m_SDLWindow)) {
        throw std::runtime_error(std::string("SDL_ClaimWindowForGPUDevice(): ") + SDL_GetError());
    }
    SDL_SetGPUSwapchainParameters(m_GPUDevice, m_SDLWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                  SDL_GPU_PRESENTMODE_VSYNC);

    SDL_SetWindowPosition(m_SDLWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(m_SDLWindow);
}

/**
 * @brief   Creates the ImGui and ImPlot contexts, applies the theme and the bundled font, and binds the SDL3 and
 *          SDL GPU backends.
 * @note    The theme choice is kept in imgui.ini by its settings handler, read on the first frame.
 */
void Application::InitImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    // The layout and the theme go to the user's data folder (~/.local/share/siggen on Linux, or under
    // XDG_DATA_HOME), so they do not depend on where the program starts or need a writable install folder
    if (char *pref_path = SDL_GetPrefPath(nullptr, "siggen")) {
        m_SettingsPath = std::string(pref_path) + "imgui.ini";
        SDL_free(pref_path);
        io.IniFilename = m_SettingsPath.c_str();
    }
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    // Windows float and dock inside the main window only
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ApplyTheme();
    LoadThemeFonts();
    RegisterThemeSettingsHandler();

    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(m_WindowScale);
    style.FontScaleDpi = m_WindowScale;
    io.ConfigDpiScaleFonts = true;

    ImGui_ImplSDL3_InitForSDLGPU(m_SDLWindow);
    ImGui_ImplSDLGPU3_InitInfo init_info{};
    init_info.Device = m_GPUDevice;
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(m_GPUDevice, m_SDLWindow);
    ImGui_ImplSDLGPU3_Init(&init_info);
}

/**
 * @brief   Sets the window icon from assets/icon.png, relative to the working directory.
 * @note    A missing icon only logs a warning.
 */
void Application::LoadAndSetIcon() {
    const std::string icon_path = "assets/icon.png";
    int width = 0;
    int height = 0;
    int channels = 0;
    constexpr int RGBAChannels = 4;
    unsigned char *pixels = stbi_load(icon_path.c_str(), &width, &height, &channels, RGBAChannels);
    if (pixels == nullptr) {
        spdlog::warn("Unable to load window icon: {}", icon_path);
        return;
    }

    SDL_Surface *icon = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels, width * RGBAChannels);
    if (icon != nullptr) {
        SDL_SetWindowIcon(m_SDLWindow, icon);
        SDL_DestroySurface(icon);
    }
    stbi_image_free(pixels);
}

void Application::PollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        const bool main_window_closed =
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(m_SDLWindow);
        if (event.type == SDL_EVENT_QUIT || main_window_closed) {
            // The window manager asks before unsaved changes are lost, and the loop stops once it confirms.
            // Minimized windows draw no frames, so the window is restored for the question to show up
            m_WindowManager.RequestQuit();
            if (SDL_GetWindowFlags(m_SDLWindow) & SDL_WINDOW_MINIMIZED) {
                SDL_RestoreWindow(m_SDLWindow);
            }
        }
    }
}

void Application::NewFrame() {
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

// The window title names the document, with an asterisk while it has unsaved changes, as editors do
void Application::UpdateWindowTitle() {
    const GeneratorSession &session = m_WindowManager.GetSession();
    const std::string title =
        std::format("{}{} - siggen", session.GetDocumentName().empty() ? "Untitled" : session.GetDocumentName(),
                    session.IsModified() ? " *" : "");
    if (title != m_WindowTitle) {
        SDL_SetWindowTitle(m_SDLWindow, title.c_str());
        m_WindowTitle = title;
    }
}

void Application::EndFrame() {
    SDL_GPUCommandBuffer *command_buffer = SDL_AcquireGPUCommandBuffer(m_GPUDevice);
    if (command_buffer == nullptr) {
        return;
    }

    ImDrawData *draw_data = ImGui::GetDrawData();
    SDL_GPUTexture *swapchain_texture = nullptr;
    const bool is_minimized = draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f;
    if (!is_minimized &&
        SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, m_SDLWindow, &swapchain_texture, nullptr, nullptr) &&
        swapchain_texture != nullptr) {
        // Uploads the vertex and index buffers, which cannot happen inside a render pass
        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);

        const ImVec4 background = GetThemeBackground();
        SDL_GPUColorTargetInfo target_info{};
        target_info.texture = swapchain_texture;
        target_info.clear_color = SDL_FColor{background.x, background.y, background.z, background.w};
        target_info.load_op = SDL_GPU_LOADOP_CLEAR;
        target_info.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *render_pass = SDL_BeginGPURenderPass(command_buffer, &target_info, 1, nullptr);
        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);
        SDL_EndGPURenderPass(render_pass);
    }

    SDL_SubmitGPUCommandBuffer(command_buffer);
}

/**
 * @brief   Reports an error raised while running a frame.
 * @param[in] title    Short context, such as "Runtime Error".
 * @param[in] message  The error text.
 */
void Application::ShowErrorMessage(const std::string &title, const std::string &message) {
    spdlog::error("{}: {}", title, message);
    // A program started from a file manager or the Windows release build has no console to show stderr
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "siggen", message.c_str(), m_SDLWindow);
}

} // namespace GUI
