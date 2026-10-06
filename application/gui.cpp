#include "app.h"
#include "generator.h"
#include "imgui_layer.h"
#include "window_manager.h"
#include <spdlog/spdlog.h>

int run_gui(const iq::GenerationConfig& config)
{
    constexpr int SCENE_WIDTH = 1280;
    constexpr int SCENE_HEIGHT = 800;
    App           application = App(SCENE_WIDTH, SCENE_HEIGHT);
    WindowManager windowManager(config);

    spdlog::debug("Initializing application");
    application.Init();
    spdlog::info("Application initialized");

    // Main loop
    spdlog::debug("Entering main loop");
    while (!application.GetShouldClose())
    {
        try
        {
            // Get + Handle User Input
            glfwPollEvents();

            // Everything regarding ImGui needs to be used between ImGuiLayer::newFrame() and ImGuiLayer::render()
            ImGuiLayer::NewFrame();
            windowManager.Render();
            ImGuiLayer::Render();
            application.SwapBuffers();
        }
        catch (const std::exception& e)
        {
            application.ShowErrorMessage("Runtime Error", e.what());
            // Continue running to allow user to see the error
        }
    }
    spdlog::info("Application closing");
    return 0;
}
