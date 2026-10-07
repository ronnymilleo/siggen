/**
 * @file    imgui_window_layer.h
 * @brief   Base class for an ImGui window that fills the native window.
 */

#ifndef SIGGEN_IMGUI_WINDOW_LAYER_H
#define SIGGEN_IMGUI_WINDOW_LAYER_H

#include "imgui.h"
#include <string>

namespace GUI {

/**
 * @class   ImGuiWindowLayer
 * @brief   An ImGui window pinned to the work area of the main viewport, around the content of a derived class.
 * @details Derived classes implement DrawContents() with the window content only. The title doubles as the ImGui
 *          ID of the window.
 */
class ImGuiWindowLayer {
public:
    explicit ImGuiWindowLayer(const std::string &title);
    virtual ~ImGuiWindowLayer() = default;

    virtual void Render();

protected:
    virtual void DrawContents() = 0;

private:
    std::string m_WindowTitle;
};

} // namespace GUI

#endif // SIGGEN_IMGUI_WINDOW_LAYER_H
