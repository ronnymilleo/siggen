/**
 * @file    window_manager.cpp
 * @brief   Owns the generator session and its windows, and lays the windows out in a dockspace.
 */

#include "window_manager.h"

#include "imgui_internal.h"

namespace GUI {

/**
 * @brief   Creates the session and its windows.
 * @param[in] config  Initial generator settings.
 */
WindowManager::WindowManager(const Core::GenerationConfig &config) : m_Session(config) {
}

/**
 * @brief   Picks up a finished generation, then draws the menu bar, the dockspace and every window. Call once per
 *          frame, between ImGuiLayer::NewFrame() and ImGuiLayer::Render().
 * @note    Needs ImGuiConfigFlags_DockingEnable, which ImGuiLayer::Init() sets.
 */
void WindowManager::Render() {
    m_Session.Update();
    // The menu bar goes first so the dockspace fills the space left below it
    DrawMainMenuBar();

    // A fixed ID lets the layout be rebuilt before the dockspace is submitted this frame
    const ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
    if (m_ResetLayout) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        m_ResetLayout = false;
    }
    // No node means imgui.ini has no saved layout, or it was just reset, so the default one is built
    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        SetupDefaultLayout(dockspace_id);
    }
    ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport());

    m_ControlsWindow.Render();
    m_SummaryWindow.Render();
    m_ViewsWindow.Render();
}

void WindowManager::DrawMainMenuBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("View")) {
        // Render() applies it right after the menu
        if (ImGui::MenuItem("Reset Layout")) {
            m_ResetLayout = true;
        }
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// Signal Setup on the left, Signal Summary above Signal Views on the right. Must run before the windows are drawn.
// Each split returns the new node in the given direction and leaves the rest in its last argument
void WindowManager::SetupDefaultLayout(ImGuiID dockspace_id) {
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);
    ImGuiID results_id = dockspace_id;
    const ImGuiID controls_id = ImGui::DockBuilderSplitNode(results_id, ImGuiDir_Left, 0.36f, nullptr, &results_id);
    ImGuiID views_id = results_id;
    const ImGuiID summary_id = ImGui::DockBuilderSplitNode(views_id, ImGuiDir_Up, 0.3f, nullptr, &views_id);
    ImGui::DockBuilderDockWindow(m_ControlsWindow.GetWindowTitle().c_str(), controls_id);
    ImGui::DockBuilderDockWindow(m_SummaryWindow.GetWindowTitle().c_str(), summary_id);
    ImGui::DockBuilderDockWindow(m_ViewsWindow.GetWindowTitle().c_str(), views_id);
    ImGui::DockBuilderFinish(dockspace_id);
}

} // namespace GUI
