/**
 * @file    gui_runner.cpp
 * @brief   Entry point of the graphical interface.
 */

#include "gui_runner.h"

#include "application.h"
#include "generator.h"
#include "imgui_layer.h"
#include "window_manager.h"
#include <spdlog/spdlog.h>

namespace GUI {

/**
 * @brief   Opens the main window and runs the frame loop until the user closes it.
 * @param[in] config  Initial generator settings, already resolved from the preset and command-line options.
 * @return  0 when the window is closed normally.
 * @note    Errors raised while drawing a frame are logged and the loop continues; initialization errors propagate.
 */
int RunGUI(const Core::GenerationConfig &config) {
    constexpr int SceneWidth = 1280;
    constexpr int SceneHeight = 800;
    Application application = Application(SceneWidth, SceneHeight);
    WindowManager window_manager(config);

    spdlog::debug("Initializing application");
    application.Init();
    spdlog::info("Application initialized");

    spdlog::debug("Entering main loop");
    while (!application.GetShouldClose()) {
        try {
            glfwPollEvents();
            ImGuiLayer::NewFrame();
            window_manager.Render();
            ImGuiLayer::Render();
            application.SwapBuffers();
        } catch (const std::exception &error) {
            // Keep running so the user can see the error and carry on
            application.ShowErrorMessage("Runtime Error", error.what());
        }
    }
    spdlog::info("Application closing");
    return 0;
}

} // namespace GUI
