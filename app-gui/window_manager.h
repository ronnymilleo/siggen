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
 * @brief   Holds the session and the windows that edit and show it, and draws them each frame in a dockspace that
 *          fills the main viewport below a menu bar.
 * @details The default layout puts Signal Setup on the left and Signal Summary above Signal Views on the right. It
 *          is built when imgui.ini has no saved layout, and again from View > Reset Layout.
 */
class WindowManager {
public:
    explicit WindowManager(const Core::GenerationConfig &config = {});

    void Render();

private:
    friend struct GUITestAccess;

    void DrawMainMenuBar();
    void SetupDefaultLayout(ImGuiID dockspace_id);

    bool m_ResetLayout = false;

    // The session is declared first, so it exists when the windows that refer to it are built
    GeneratorSession m_Session;
    ControlsWindow m_ControlsWindow{m_Session};
    SummaryWindow m_SummaryWindow{m_Session};
    ViewsWindow m_ViewsWindow{m_Session};
};

} // namespace GUI

#endif // SIGGEN_WINDOW_MANAGER_H
