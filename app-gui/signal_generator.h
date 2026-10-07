/**
 * @file    signal_generator.h
 * @brief   The generator window: settings, generation, measurements, plots and export.
 */

#ifndef SIGGEN_SIGNAL_GENERATOR_H
#define SIGGEN_SIGNAL_GENERATOR_H

#include "generation_job.h"
#include "imgui_window_layer.h"
#include "iq_export.h"
#include "measurements.h"
#include "pipeline.h"
#include "plot_data.h"
#include "plot_export.h"
#include <functional>
#include <optional>
#include <string>

namespace GUI {

/**
 * @class   SignalGenerator
 * @brief   Edits the generation settings, runs the generation in the background and shows and exports its result.
 * @details The left pane holds the settings and actions, the right pane the summary, measurements and plots of the
 *          last result. Analysis is recomputed once per finished generation, not per frame. The result shown may be
 *          older than the settings; the summary says so.
 */
class SignalGenerator : public ImGuiWindowLayer {
public:
    explicit SignalGenerator(Core::GenerationConfig config = {});
    ~SignalGenerator() override = default;

protected:
    void DrawContents() override;

private:
    friend struct SignalGeneratorTestAccess;

    // Generation and analysis
    void PollFinishedJob();
    void RefreshAnalysis(const Core::GeneratedSignal &result);
    void UpdateSpectrum();
    void ResetBitInput();

    // Controls
    void DrawControls();
    void DrawModulationSelector();
    void DrawNoiseSourceControls();
    void DrawLinearControls(bool fsk);
    void DrawFrequencyModulationControls();
    void DrawPulseShapingControls();
    void DrawAwgnControls();
    void DrawImpairmentControls();
    void DrawSeedControls();
    void DrawAdvancedControls();
    void DrawActions(bool settings_valid);
    void DrawPresets();

    // Results
    void DrawSummary();
    void DrawNoiseSummary(const Core::GeneratedSignal &result);
    void DrawSymbolSummary(const Core::GeneratedSignal &result);
    void DrawMeasurements(const Core::GeneratedSignal &result);
    void DrawPlots();
    void DrawWaveformTab();
    void DrawFrequencyTab();
    void DrawConstellationTab(bool noisy);
    void DrawEyeTab();
    void DrawPipelineTab(const Core::GeneratedSignal &result);
    void DrawPipelineControls();
    void DrawPipelineStages(const Core::GeneratedSignal &result);
    void DrawSpectrumTab();

    // Export
    void DrawExportDialog();
    void Export(bool overwrite);
    void ExportImageButton(const char *stem, const std::function<Core::Figure()> &build);
    void DrawImageExportDialog();
    void ExportImage(bool overwrite);

    // Settings
    Core::GenerationConfig m_Config;
    std::vector<char> m_BitInput; // Null-terminated edit buffer for explicit bits
    char m_PresetPath[1024] = "default.preset";
    std::string m_PresetNotes;

    // Generation
    Core::GenerationJob m_Job;
    std::string m_Error;

    // Analysis of the last result
    Core::PlotData m_Plots;
    Core::Spectrum m_Spectrum;
    std::vector<double> m_SpectrumDb;
    Core::PowerStatistics m_Power;
    std::optional<Core::SymbolAccuracy> m_Accuracy;
    Core::EyeDiagram m_Eye;
    std::optional<Core::PipelineStages> m_Pipeline; // Linear waveforms only

    // View state
    bool m_WaveformFit = true;
    bool m_SpectrumFit = true;
    int m_ConstellationView = 0; // 0: mapped symbols, 1: matched filter
    Core::Window m_Window = Core::Window::Hann;
    int m_EyeComponent = 0; // 0: I, 1: Q
    int m_PipelineFirst = 0;
    int m_PipelineCount = 12;
    bool m_PipelineAlign = true;
    bool m_PipelineFit = true;

    // I/Q export
    char m_ExportPath[1024] = "signal.csv";
    int m_ExportFormat = 0;
    std::shared_ptr<const Core::GeneratedSignal> m_ExportResult;
    std::string m_ExportStatus;
    bool m_ConfirmOverwrite = false;

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

#endif // SIGGEN_SIGNAL_GENERATOR_H
