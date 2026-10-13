/**
 * @file    window_manager.h
 * @brief   Owns the generator session and its windows, and lays the windows out in a dockspace.
 */

#ifndef SIGGEN_WINDOW_MANAGER_H
#define SIGGEN_WINDOW_MANAGER_H

#include "generator_session.h"
#include "imgui.h"
#include "windows/controls_window.h"
#include "windows/summary_window.h"
#include "windows/views_window.h"

namespace GUI {

/**
 * @class   WindowManager
 * @brief   Holds the session and the windows that edit and show it, draws them each frame in a dockspace that
 *          fills the main viewport below a menu bar, and owns the File and View menus.
 * @details The default layout puts Signal Setup on the left and Signal Summary above Signal Views on the right. It
 *          is built when imgui.ini has no saved layout, and again from View > Reset Layout. The File menu opens and
 *          saves presets, lists the compiled-in examples and quits; quitting with unsaved settings asks first.
 */
class WindowManager {
public:
    explicit WindowManager(const Core::GenerationConfig &config = {});

    void Render();
    void RequestQuit();
    bool IsQuitConfirmed() const;
    const GeneratorSession &GetSession() const;

private:
    friend struct GUITestAccess;

    void DrawMainMenuBar();
    void DrawFileMenu();
    void DrawExamplesMenu();
    void DrawViewMenu();
    void HandleShortcuts();
    void DrawQuitConfirmation();
    void DrawOpenDialog();
    void DrawSaveDialog();
    void SavePreset();
    void OpenSaveDialog();
    void SetupDefaultLayout(ImGuiID dockspace_id);

    bool m_ResetLayout = false;

    // File menu state
    bool m_QuitRequested = false;
    bool m_QuitConfirmed = false;
    bool m_QuitPopupOpen = false;
    bool m_QuitAfterSave = false;
    bool m_OpenDialogOpen = false;
    bool m_SaveDialogOpen = false;
    char m_DialogPath[1024] = {};

    // The session is declared first, so it exists when the windows that refer to it are built
    GeneratorSession m_Session;
    ControlsWindow m_ControlsWindow{m_Session};
    SummaryWindow m_SummaryWindow{m_Session};
    ViewsWindow m_ViewsWindow{m_Session};
};

} // namespace GUI

#endif // SIGGEN_WINDOW_MANAGER_H
