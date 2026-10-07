/**
 * @file    window_manager.h
 * @brief   Owns the ImGui windows of the application and draws them each frame.
 */

#ifndef SIGGEN_WINDOW_MANAGER_H
#define SIGGEN_WINDOW_MANAGER_H

#include "imgui_window_layer.h"
#include "signal_generator.h"

namespace GUI {

/**
 * @class   WindowManager
 * @brief   Holds the application windows; today only the signal generator.
 */
class WindowManager {
public:
    explicit WindowManager(const Core::GenerationConfig &config = {});
    virtual ~WindowManager();

    void Render();

private:
    SignalGenerator m_SignalGeneratorWindow;
};

} // namespace GUI

#endif // SIGGEN_WINDOW_MANAGER_H
