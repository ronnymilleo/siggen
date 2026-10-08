/**
 * @file    generator_session.h
 * @brief   The generator document shared by every window: settings, background generation and analysis of the result.
 */

#ifndef SIGGEN_GENERATOR_SESSION_H
#define SIGGEN_GENERATOR_SESSION_H

#include "ber.h"
#include "ber_job.h"
#include "demodulator.h"
#include "generation_job.h"
#include "generator.h"
#include "measurements.h"
#include "pipeline.h"
#include "plot_data.h"
#include "signal_analysis.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace GUI {

/**
 * @class   GeneratorSession
 * @brief   The settings being edited and the last generated signal, shared by the windows that edit or show them.
 * @details Generation and BER sweeps run in the background; Update() picks up a finished sweep or generation once
 *          per frame and computes everything the windows show for a new result (plot data, measurements, bit
 *          errors, eye diagram, pipeline stages, spectrum), so
 *          the windows only read. The result may be older than the settings; compare its Config with GetConfig().
 *          Windows keep their own view state (zoom, selected view, dialogs) and reset it when GetResultVersion() or
 *          GetSpectrumVersion() changes. Failures of any operation are kept as one user-facing error message.
 */
class GeneratorSession {
public:
    explicit GeneratorSession(Core::GenerationConfig config = {});

    // Settings
    Core::GenerationConfig &GetConfig();
    const Core::GenerationConfig &GetConfig() const;
    bool SavePreset(const std::string &path);
    bool LoadPreset(const std::string &path);
    const std::string &GetPresetNotes() const;

    // Generation
    void StartGeneration();
    bool IsGenerating() const;
    void Update();
    const std::shared_ptr<const Core::GeneratedSignal> &GetResult() const;
    std::size_t GetResultVersion() const;

    // Errors
    const std::string &GetError() const;
    void SetError(const std::string &message);

    // Analysis of the last result
    const Core::PlotData &GetPlots() const;
    const Core::PowerStatistics &GetPowerStatistics() const;
    const std::optional<Core::SymbolAccuracy> &GetSymbolAccuracy() const;
    const std::optional<Core::BitErrors> &GetBitErrors() const;
    std::optional<double> GetMeasuredEbN0Db() const;
    const Core::EyeDiagram &GetEyeDiagram() const;
    const std::optional<Core::PipelineStages> &GetPipeline() const;

    // Spectrum of the last result
    const Core::Spectrum &GetSpectrum() const;
    const std::vector<double> &GetSpectrumDb() const;
    Core::Window GetSpectrumWindow() const;
    void SetSpectrumWindow(Core::Window window);
    std::size_t GetSpectrumVersion() const;

    // BER sweep
    void StartBerSweep(Core::BerSweepSettings settings);
    void CancelBerSweep();
    const Core::BerJob &GetBerSweep() const;
    std::size_t GetBerVersion() const;

private:
    void RefreshAnalysis(const Core::GeneratedSignal &result);
    void UpdateSpectrum();

    // Settings
    Core::GenerationConfig m_Config;
    std::string m_PresetNotes;

    // Generation
    Core::GenerationJob m_Job;
    std::size_t m_ResultVersion = 0;
    std::string m_Error;

    // Analysis of the last result
    Core::PlotData m_Plots;
    Core::PowerStatistics m_Power;
    std::optional<Core::SymbolAccuracy> m_Accuracy;
    std::optional<Core::BitErrors> m_Errors; // Reference demodulator against the transmitted bits
    Core::EyeDiagram m_Eye;
    std::optional<Core::PipelineStages> m_Pipeline; // Linear waveforms only

    // Spectrum of the last result
    Core::Window m_SpectrumWindow = Core::Window::Hann;
    Core::Spectrum m_Spectrum;
    std::vector<double> m_SpectrumDb;
    std::size_t m_SpectrumVersion = 0;

    // BER sweep
    Core::BerJob m_BerJob;
    std::size_t m_BerVersion = 0;
};

} // namespace GUI

#endif // SIGGEN_GENERATOR_SESSION_H
