/**
 * @file    window_manager.cpp
 * @brief   Owns the generator session and its windows, and lays the windows out in a dockspace.
 */

#include "window_manager.h"

#include "examples.h"
#include "imgui_internal.h"
#include "shortcuts.h"
#include "theme.h"
#include <cstddef>
#include <cstdio>
#include <string>

namespace GUI {

namespace {

// Wrap width of the example descriptions, in characters of the current font size
constexpr float ExampleTooltipWidth = 30.0f;

} // namespace

/**
 * @brief   Creates the session and its windows.
 * @param[in] config  Initial generator settings.
 */
WindowManager::WindowManager(const Core::GenerationConfig &config) : m_Session(config) {
}

/**
 * @brief   Picks up a finished generation, then draws the menus, the dockspace, every window and the open dialogs.
 *          Call once per frame, between Application::NewFrame() and ImGui::Render().
 * @note    Needs ImGuiConfigFlags_DockingEnable, which Application::InitImGui() sets.
 */
void WindowManager::Render() {
    m_Session.Update();
    HandleShortcuts();
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

    DrawQuitConfirmation();
    DrawOpenDialog();
    DrawSaveDialog();
}

/**
 * @brief   Asks for the application to close; unsaved settings changes are confirmed first.
 * @note    Called by the File menu, the quit shortcut and the window close button alike.
 */
void WindowManager::RequestQuit() {
    if (!m_Session.IsModified()) {
        m_QuitConfirmed = true;
        return;
    }
    m_QuitRequested = true;
}

/**
 * @brief   Tells whether the user confirmed closing the application.
 * @return  True once the loop may stop.
 */
bool WindowManager::IsQuitConfirmed() const {
    return m_QuitConfirmed;
}

/**
 * @brief   Returns the document the windows edit and show.
 * @return  The session, for the window title.
 */
const GeneratorSession &WindowManager::GetSession() const {
    return m_Session;
}

void WindowManager::DrawMainMenuBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    DrawFileMenu();
    DrawViewMenu();
    ImGui::EndMainMenuBar();
}

void WindowManager::DrawFileMenu() {
    if (!ImGui::BeginMenu("File")) {
        return;
    }
    if (ImGui::MenuItem("Open Preset...", SIGGEN_SHORTCUT "O")) {
        std::snprintf(m_DialogPath, sizeof m_DialogPath, "%s", m_Session.GetDocumentPath().c_str());
        m_OpenDialogOpen = true;
    }
    if (ImGui::MenuItem("Save Preset", SIGGEN_SHORTCUT "S")) {
        SavePreset();
    }
    if (ImGui::MenuItem("Save Preset As...", SIGGEN_SHORTCUT "Shift+S")) {
        OpenSaveDialog();
    }
    ImGui::Separator();
    DrawExamplesMenu();
    ImGui::Separator();
    if (ImGui::MenuItem("Quit", SIGGEN_SHORTCUT "Q")) {
        RequestQuit();
    }
    ImGui::EndMenu();
}

void WindowManager::DrawExamplesMenu() {
    if (!ImGui::BeginMenu("Examples")) {
        return;
    }
    for (const Example &example : GetExamples()) {
        if (ImGui::MenuItem(example.Title.c_str())) {
            m_Session.LoadPresetText(std::string(example.Text), example.Title);
        }
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * ExampleTooltipWidth);
            ImGui::TextUnformatted(example.Description.data(), example.Description.data() + example.Description.size());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    ImGui::EndMenu();
}

void WindowManager::DrawViewMenu() {
    if (!ImGui::BeginMenu("View")) {
        return;
    }
    if (ImGui::BeginMenu("Theme")) {
        const auto themes = GetThemes();
        for (std::size_t index = 0; index < themes.size(); ++index) {
            if (ImGui::MenuItem(themes[index].Name, nullptr, index == GetThemeIndex())) {
                SetTheme(index);
                ImGui::MarkIniSettingsDirty();
            }
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    // Render() applies it right after the menu
    if (ImGui::MenuItem("Reset Layout")) {
        m_ResetLayout = true;
    }
    ImGui::EndMenu();
}

// ImGuiMod_Shortcut is Ctrl everywhere but macOS, where it is Cmd, matching the menu labels
void WindowManager::HandleShortcuts() {
    if (ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiKey_O)) {
        std::snprintf(m_DialogPath, sizeof m_DialogPath, "%s", m_Session.GetDocumentPath().c_str());
        m_OpenDialogOpen = true;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_S)) {
        OpenSaveDialog();
    } else if (ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiKey_S)) {
        SavePreset();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiKey_Q)) {
        RequestQuit();
    }
}

void WindowManager::DrawQuitConfirmation() {
    if (m_QuitConfirmed || !m_QuitRequested) {
        return;
    }
    if (!m_QuitPopupOpen) {
        ImGui::OpenPopup("Unsaved Changes");
        m_QuitPopupOpen = true;
    }
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The settings differ from the last preset loaded or saved.");
        ImGui::Separator();
        if (ImGui::Button("Save and Quit")) {
            m_QuitAfterSave = true;
            if (m_Session.GetDocumentPath().empty()) {
                OpenSaveDialog();
            } else {
                SavePreset();
            }
            m_QuitRequested = false;
            m_QuitPopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Quit Without Saving")) {
            m_QuitConfirmed = true;
            m_QuitRequested = false;
            m_QuitPopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_QuitRequested = false;
            m_QuitPopupOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void WindowManager::DrawOpenDialog() {
    if (m_OpenDialogOpen) {
        ImGui::OpenPopup("Open Preset");
        m_OpenDialogOpen = false;
    }
    if (ImGui::BeginPopupModal("Open Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushItemWidth(420.0f);
        ImGui::InputText("Path", m_DialogPath, sizeof m_DialogPath);
        ImGui::PopItemWidth();
        ImGui::TextDisabled("Preset file to replace the settings with.");
        if (ImGui::Button("Open")) {
            m_Session.LoadPreset(m_DialogPath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void WindowManager::DrawSaveDialog() {
    if (m_SaveDialogOpen) {
        ImGui::OpenPopup("Save Preset");
        m_SaveDialogOpen = false;
    }
    if (ImGui::BeginPopupModal("Save Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushItemWidth(420.0f);
        ImGui::InputText("Path", m_DialogPath, sizeof m_DialogPath);
        ImGui::PopItemWidth();
        ImGui::TextDisabled("Existing files are replaced; the parent directory must exist.");
        if (ImGui::Button("Save")) {
            if (m_Session.SavePreset(m_DialogPath)) {
                if (m_QuitAfterSave) {
                    m_QuitConfirmed = true;
                    m_QuitAfterSave = false;
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_QuitAfterSave = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Without a path yet, saving asks for a destination first
void WindowManager::SavePreset() {
    const auto &path = m_Session.GetDocumentPath();
    if (path.empty()) {
        OpenSaveDialog();
        return;
    }
    if (m_Session.SavePreset(path) && m_QuitAfterSave) {
        m_QuitConfirmed = true;
        m_QuitAfterSave = false;
    }
}

void WindowManager::OpenSaveDialog() {
    const auto &path = m_Session.GetDocumentPath();
    std::snprintf(m_DialogPath, sizeof m_DialogPath, "%s", path.empty() ? "custom.preset" : path.c_str());
    m_SaveDialogOpen = true;
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
