/**
 * @file    imgui_window_layer.cpp
 * @brief   Base class for an ImGui window that fills the native window.
 */

#include "imgui_window_layer.h"

namespace GUI {

/**
 * @brief   Creates the window layer.
 * @param[in] title  Window title, also used as its ImGui ID.
 */
ImGuiWindowLayer::ImGuiWindowLayer(const std::string &title) : m_WindowTitle{title} {
}

/**
 * @brief   Draws the window over the whole work area of the main viewport, then its content. Call once per frame.
 * @note    Position, size and viewport are forced every frame, so the window follows the native window after a
 *          resize, and an old imgui.ini that placed it in a detached viewport has no effect.
 */
void ImGuiWindowLayer::Render() {
    const auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    constexpr auto Flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin(m_WindowTitle.c_str(), nullptr, Flags)) {
        DrawContents();
    }
    ImGui::End();
}

} // namespace GUI
