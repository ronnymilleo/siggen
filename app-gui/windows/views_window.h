/**
 * @file    views_window.h
 * @brief   The Signal Views window: plots of the last result and their image export.
 */

#ifndef SIGGEN_VIEWS_WINDOW_H
#define SIGGEN_VIEWS_WINDOW_H

#include "app_window.h"
#include "generator_session.h"
#include "plot_export.h"
#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace GUI {

/**
 * @class   ViewsWindow
 * @brief   Shows the last result as tabs of plots (waveform, frequency, constellation, eye, pipeline, spectrum) and
 *          saves any of them as a PNG or SVG image.
 * @details Each tab appears only for the waveform families it applies to. The window keeps the view state (zoom
 *          requests, selected component, pipeline window, image dialog) and resets it when the session analyses a new
 *          result.
 */
class ViewsWindow : public AppWindow {
public:
    explicit ViewsWindow(GeneratorSession &session);

private:
    friend struct GUITestAccess;

    void Draw() override;
    void FollowSession();

    // Tabs
    void DrawWaveformTab();
    void DrawFrequencyTab();
    void DrawConstellationTab(bool noisy);
    void DrawEyeTab();
    void DrawPipelineTab(const Core::GeneratedSignal &result);
    void DrawPipelineControls();
    void DrawPipelineStages(const Core::GeneratedSignal &result);
    void DrawSpectrumTab();

    // Image export
    void ExportImageButton(const char *stem, const std::function<Core::Figure()> &build);
    void DrawImageExportDialog();
    void ExportImage(bool overwrite);

    GeneratorSession &m_Session;
    std::size_t m_SeenResultVersion = 0;
    std::size_t m_SeenSpectrumVersion = 0;

    // View state
    bool m_WaveformFit = true;
    bool m_SpectrumFit = true;
    int m_ConstellationView = 0; // 0: mapped symbols, 1: matched filter
    int m_EyeComponent = 0;      // 0: I, 1: Q
    int m_PipelineFirst = 0;
    int m_PipelineCount = 12;
    bool m_PipelineAlign = true;
    bool m_PipelineFit = true;

    // Image export of the plot on screen (PNG or SVG), for slides and reports
    std::optional<Core::Figure> m_ImageFigure;
    bool m_ImageOpenRequested = false;
    char m_ImagePath[1024] = "siggen-plot.png";
    int m_ImageFormat = 0;
    int m_ImageWidth = 1600;
    int m_ImageHeight = 900;
    int m_ImageTheme = 0;
    std::string m_ImageStatus;
    bool m_ImageConfirmOverwrite = false;
};

} // namespace GUI

#endif // SIGGEN_VIEWS_WINDOW_H
