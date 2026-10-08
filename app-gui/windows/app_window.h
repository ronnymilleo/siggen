/**
 * @file    app_window.h
 * @brief   Base class for the dockable ImGui windows of the application.
 */

#ifndef SIGGEN_APP_WINDOW_H
#define SIGGEN_APP_WINDOW_H

#include "imgui.h"
#include <string>

namespace GUI {

/**
 * @class   AppWindow
 * @brief   An ImGui window that opens and closes its own Begin/End pair around the content of a derived class.
 * @details Derived classes implement Draw() with the window content only. The title doubles as the ImGui ID, so it
 *          also names the window in the docking layout and in imgui.ini. The windows cannot be closed; View > Reset
 *          Layout brings back one that was undocked.
 */
class AppWindow {
public:
    explicit AppWindow(std::string title, ImGuiWindowFlags flags = ImGuiWindowFlags_None);
    virtual ~AppWindow() = default;

    void Render();
    const std::string &GetWindowTitle() const;

private:
    virtual void Draw() = 0;

    std::string m_WindowTitle;
    ImGuiWindowFlags m_WindowFlags;
};

} // namespace GUI

#endif // SIGGEN_APP_WINDOW_H
