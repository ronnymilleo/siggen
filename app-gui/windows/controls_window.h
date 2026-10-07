/**
 * @file    controls_window.h
 * @brief   The Signal Setup window: generation settings, Generate and I/Q export, and presets.
 */

#ifndef SIGGEN_CONTROLS_WINDOW_H
#define SIGGEN_CONTROLS_WINDOW_H

#include "app_window.h"
#include "generator.h"
#include "generator_session.h"
#include <memory>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   ControlsWindow
 * @brief   Edits the settings of the session, starts the generation, exports the last result as I/Q samples and
 *          saves or loads presets.
 * @details Only the controls of the selected waveform family are shown. Invalid settings are explained below the
 *          controls and disable Generate. The window owns the edit buffers and the export dialog; the settings live
 *          in the session.
 */
class ControlsWindow : public AppWindow {
public:
    explicit ControlsWindow(GeneratorSession &session);

private:
    void Draw() override;

    // Settings
    void DrawModulationSelector();
    void DrawNoiseSourceControls();
    void DrawLinearControls(bool fsk);
    void DrawFrequencyModulationControls();
    void DrawPulseShapingControls();
    void DrawAwgnControls();
    void DrawImpairmentControls();
    void DrawSeedControls();
    void DrawAdvancedControls();
    void ResetBitInput();

    // Actions
    void DrawActions(bool settings_valid);
    void DrawPresets();

    // I/Q export
    void DrawExportDialog();
    void Export(bool overwrite);

    GeneratorSession &m_Session;

    // Edit buffers
    std::vector<char> m_BitInput; // Null-terminated edit buffer for explicit bits
    char m_PresetPath[1024] = "default.preset";

    // I/Q export
    char m_ExportPath[1024] = "signal.csv";
    int m_ExportFormat = 0;
    std::shared_ptr<const Core::GeneratedSignal> m_ExportResult;
    std::string m_ExportStatus;
    bool m_ConfirmOverwrite = false;
};

} // namespace GUI

#endif // SIGGEN_CONTROLS_WINDOW_H
