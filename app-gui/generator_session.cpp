/**
 * @file    generator_session.cpp
 * @brief   The generator document shared by every window: settings, background generation and analysis of the result.
 */

#include "generator_session.h"

#include "preset.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>
#include <utility>

namespace GUI {

namespace {

void LogGenerated(const Core::GeneratedSignal &result) {
    if (result.Family == Core::Family::Noise) {
        spdlog::info("Generated {}: {} complex samples at {} Hz", Core::ModulationName(result.Config.Modulation),
                     result.Samples.size(), result.SampleRateHz);
    } else {
        spdlog::info("Generated {}: {} symbols, {} complex samples at {} Hz",
                     Core::ModulationName(result.Config.Modulation),
                     result.Family == Core::Family::Fsk ? result.SymbolFrequenciesHz.size() : result.Symbols.size(),
                     result.Samples.size(), result.SampleRateHz);
    }
}

} // namespace

/**
 * @brief   Creates a session with initial settings and no result.
 * @param[in] config  Initial settings.
 */
GeneratorSession::GeneratorSession(Core::GenerationConfig config) : m_Config(std::move(config)) {
}

/**
 * @brief   Returns the settings being edited.
 * @return  The settings; windows edit them in place.
 */
Core::GenerationConfig &GeneratorSession::GetConfig() {
    return m_Config;
}

/**
 * @brief   Returns the settings being edited.
 * @return  The settings.
 */
const Core::GenerationConfig &GeneratorSession::GetConfig() const {
    return m_Config;
}

/**
 * @brief   Writes the current settings to a preset file.
 * @param[in] path  Destination file.
 * @return  True on success; on failure the error is logged and kept for GetError().
 */
bool GeneratorSession::SavePreset(const std::string &path) {
    try {
        Core::SavePreset(path, m_Config);
        spdlog::info("Saved preset: {}", path);
        m_Error.clear();
        return true;
    } catch (const std::exception &error) {
        spdlog::error("Preset save failed: {}", error.what());
        m_Error = error.what();
        return false;
    }
}

/**
 * @brief   Replaces the settings with a preset file and keeps its notes.
 * @param[in] path  Preset file.
 * @return  True on success; on failure the settings stay as they were, and the error is logged and kept for
 *          GetError().
 */
bool GeneratorSession::LoadPreset(const std::string &path) {
    try {
        auto loaded = Core::LoadPreset(path);
        m_Config = std::move(loaded);
        m_PresetNotes = Core::LoadPresetNotes(path);
        spdlog::info("Loaded preset: {}", path);
        m_Error.clear();
        return true;
    } catch (const std::exception &error) {
        spdlog::error("Preset load failed: {}", error.what());
        m_Error = error.what();
        return false;
    }
}

/**
 * @brief   Returns the notes of the last loaded preset.
 * @return  The lesson notes, or an empty string when none was loaded or it had none.
 */
const std::string &GeneratorSession::GetPresetNotes() const {
    return m_PresetNotes;
}

/**
 * @brief   Starts generating a copy of the current settings in the background and clears the last error.
 * @note    Does nothing while a generation is pending. A failure to start is logged and kept for GetError().
 */
void GeneratorSession::StartGeneration() {
    try {
        if (m_Job.Start(m_Config)) {
            spdlog::debug("Started {} generation: {} symbols", Core::ModulationName(m_Config.Modulation),
                          m_Config.SymbolCount);
        }
        m_Error.clear();
    } catch (const std::exception &error) {
        spdlog::error("Unable to start generation: {}", error.what());
        m_Error = error.what();
    }
}

/**
 * @brief   Tells whether a generation is running.
 * @return  True from StartGeneration() until Update() collects the finished generation.
 */
bool GeneratorSession::IsGenerating() const {
    return m_Job.Busy();
}

/**
 * @brief   Collects a finished BER sweep, then takes the result of a finished generation and computes every
 *          analysis shown for it. Call once per frame, before the windows draw.
 * @note    A failed generation or analysis is logged and kept for GetError() instead of propagating; a failed
 *          generation keeps the previous result.
 */
void GeneratorSession::Update() {
    if (m_BerJob.Poll()) {
        ++m_BerVersion;
        if (!m_BerJob.Error().empty()) {
            m_Error = m_BerJob.Error();
        }
    }
    try {
        if (m_Job.Poll()) {
            const auto &result = *m_Job.Result();
            LogGenerated(result);
            RefreshAnalysis(result);
        }
    } catch (const std::exception &error) {
        spdlog::error("Generation or analysis failed: {}", error.what());
        m_Error = error.what();
    }
}

/**
 * @brief   Returns the last generated signal.
 * @return  The result, or null before the first generation finishes.
 */
const std::shared_ptr<const Core::GeneratedSignal> &GeneratorSession::GetResult() const {
    return m_Job.Result();
}

/**
 * @brief   Returns a counter that changes whenever a new result is analysed.
 * @return  The counter; windows compare it with the value they last saw to reset zoom and selections.
 */
std::size_t GeneratorSession::GetResultVersion() const {
    return m_ResultVersion;
}

/**
 * @brief   Returns the message of the last failed operation.
 * @return  The message, or an empty string after a successful generation start, preset save or preset load.
 */
const std::string &GeneratorSession::GetError() const {
    return m_Error;
}

/**
 * @brief   Records the failure of an operation done by a window, so every window shows the same error.
 * @param[in] message  User-facing message.
 */
void GeneratorSession::SetError(const std::string &message) {
    m_Error = message;
}

/**
 * @brief   Returns the plot-ready copy of the last result.
 * @return  Reduced waveform, constellations and frequency track; empty before the first result.
 */
const Core::PlotData &GeneratorSession::GetPlots() const {
    return m_Plots;
}

/**
 * @brief   Returns the power statistics of the last result.
 * @return  Mean and peak power and PAPR over every sample.
 */
const Core::PowerStatistics &GeneratorSession::GetPowerStatistics() const {
    return m_Power;
}

/**
 * @brief   Returns the symbol accuracy of the last result.
 * @return  EVM and matched-filter SNR, or nothing for signals without steady-state symbols.
 */
const std::optional<Core::SymbolAccuracy> &GeneratorSession::GetSymbolAccuracy() const {
    return m_Accuracy;
}

/**
 * @brief   Returns the bit errors of the reference demodulator on the last result.
 * @return  Symbol and bit errors against the transmitted bits, or nothing for waveforms without a reference
 *          receiver.
 */
const std::optional<Core::BitErrors> &GeneratorSession::GetBitErrors() const {
    return m_Errors;
}

/**
 * @brief   Returns the Eb/N0 implied by the measured SNR after the matched filter of the last result.
 * @return  Eb/N0 in dB, or nothing when the result has no symbol accuracy.
 */
std::optional<double> GeneratorSession::GetMeasuredEbN0Db() const {
    const auto &result = m_Job.Result();
    if (!result || !m_Accuracy) {
        return std::nullopt;
    }
    return Core::SnrToEnergyRatios(m_Accuracy->SnrAfterMatchedDb - m_Accuracy->ExpectedOffsetDb,
                                   result->Config.SamplesPerSymbol, Core::BitsPerSymbol(result->Config.Modulation))
        .EbN0Db;
}

/**
 * @brief   Returns the eye diagram of the last result.
 * @return  The traces; empty for noise and FSK signals.
 */
const Core::EyeDiagram &GeneratorSession::GetEyeDiagram() const {
    return m_Eye;
}

/**
 * @brief   Returns the pipeline stages of the last result.
 * @return  The stages, or nothing for noise and FSK signals.
 */
const std::optional<Core::PipelineStages> &GeneratorSession::GetPipeline() const {
    return m_Pipeline;
}

/**
 * @brief   Returns the Welch PSD of the last result.
 * @return  The spectrum, computed with GetSpectrumWindow().
 */
const Core::Spectrum &GeneratorSession::GetSpectrum() const {
    return m_Spectrum;
}

/**
 * @brief   Returns the power density of GetSpectrum() in dB.
 * @return  One value per frequency, floored at -200 dB; empty when the signal is too short for a spectrum.
 */
const std::vector<double> &GeneratorSession::GetSpectrumDb() const {
    return m_SpectrumDb;
}

/**
 * @brief   Returns the analysis window of the spectrum.
 * @return  The window used by GetSpectrum().
 */
Core::Window GeneratorSession::GetSpectrumWindow() const {
    return m_SpectrumWindow;
}

/**
 * @brief   Selects the analysis window of the spectrum and recomputes the spectrum of the last result with it.
 * @param[in] window  The new window.
 */
void GeneratorSession::SetSpectrumWindow(Core::Window window) {
    m_SpectrumWindow = window;
    UpdateSpectrum();
}

/**
 * @brief   Returns a counter that changes whenever the spectrum is recomputed.
 * @return  The counter; the spectrum view refits its axes when it changes.
 */
std::size_t GeneratorSession::GetSpectrumVersion() const {
    return m_SpectrumVersion;
}

/**
 * @brief   Starts a BER sweep on the settings of the last result.
 * @param[in] settings  Eb/N0 points and stopping rules.
 * @note    Does nothing without a result or while a sweep is running.
 */
void GeneratorSession::StartBerSweep(Core::BerSweepSettings settings) {
    const auto &result = m_Job.Result();
    if (result) {
        m_BerJob.Start(result->Config, std::move(settings));
    }
}

/**
 * @brief   Asks the running BER sweep to stop; it keeps the points finished so far.
 */
void GeneratorSession::CancelBerSweep() {
    m_BerJob.Cancel();
}

/**
 * @brief   Returns the BER sweep, for its progress and points.
 * @return  The job; read only.
 */
const Core::BerJob &GeneratorSession::GetBerSweep() const {
    return m_BerJob;
}

/**
 * @brief   Returns a counter that changes whenever a BER sweep finishes.
 * @return  The counter; the BER view refits its axes when it changes.
 */
std::size_t GeneratorSession::GetBerVersion() const {
    return m_BerVersion;
}

/**
 * @brief   Recomputes plot data, measurements, bit errors, eye diagram, pipeline stages and spectrum.
 * @param[in] result  The newly generated signal.
 */
void GeneratorSession::RefreshAnalysis(const Core::GeneratedSignal &result) {
    // Cleared and versioned first, so that a failure below leaves no analysis of the previous signal on screen
    m_Plots = {};
    m_Spectrum = {};
    m_SpectrumDb.clear();
    ++m_ResultVersion;
    ++m_SpectrumVersion;
    m_Plots = Core::MakePlotData(result);
    m_Power = Core::MeasurePowerStatistics(result.Samples);
    m_Accuracy = Core::MeasureSymbolAccuracy(result);
    m_Errors = Core::CountBitErrors(result);
    m_Eye = result.Family == Core::Family::Linear ? Core::BuildEyeDiagram(result) : Core::EyeDiagram{};
    m_Pipeline.reset();
    if (result.Family == Core::Family::Linear) {
        m_Pipeline = Core::BuildPipelineStages(result);
    }
    UpdateSpectrum();
}

/**
 * @brief   Recomputes the Welch PSD of the last result with the selected window, in dB.
 */
void GeneratorSession::UpdateSpectrum() {
    const auto &result = m_Job.Result();
    if (!result) {
        return;
    }
    m_Spectrum = Core::WelchPsd(result->Samples, result->SampleRateHz, 1024, m_SpectrumWindow);
    m_SpectrumDb.resize(m_Spectrum.PowerDensity.size());
    std::transform(m_Spectrum.PowerDensity.begin(), m_Spectrum.PowerDensity.end(), m_SpectrumDb.begin(),
                   [](double power) { return 10 * std::log10(std::max(power, 1e-20)); });
    ++m_SpectrumVersion;
}

} // namespace GUI
