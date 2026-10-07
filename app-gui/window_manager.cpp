/**
 * @file    window_manager.cpp
 * @brief   Owns the ImGui windows of the application and draws them each frame.
 */

#include "window_manager.h"

#include "signal_generator.h"

namespace GUI {

/**
 * @brief   Creates the application windows.
 * @param[in] config  Initial settings of the signal generator.
 */
WindowManager::WindowManager(const Core::GenerationConfig &config) : m_SignalGeneratorWindow(config) {
}

WindowManager::~WindowManager() = default;

/**
 * @brief   Draws every window. Call once per frame, between ImGuiLayer::NewFrame() and ImGuiLayer::Render().
 */
void WindowManager::Render() {
    m_SignalGeneratorWindow.Render();
}

} // namespace GUI
