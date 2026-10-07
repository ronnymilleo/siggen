/**
 * @file    signal_generator.cpp
 * @brief   The generator window: settings, generation, measurements, plots and export.
 */

#include "signal_generator.h"

#include "help_topics.h"
#include "imgui.h"
#include "implot.h"
#include "plot_figures.h"
#include "preset.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace GUI {

namespace {

constexpr ImVec4 ErrorColor(.96f, .45f, .40f, 1.f);
constexpr ImVec4 InPhaseColor(.2f, .6f, 1.f, 1.f);
constexpr ImVec4 QuadratureColor(1.f, .55f, .15f, 1.f);

/**
 * @brief   Shows a caption over a value, as one item of a row of metrics.
 * @param[in] label  Caption, drawn dimmed.
 * @param[in] value  Value text.
 * @param[in] first  True to start a new row instead of continuing the current one.
 */
void Metric(const char *label, const std::string &value, bool first = false) {
    if (!first) {
        ImGui::SameLine(0, 28);
    }
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", label);
    ImGui::TextUnformatted(value.c_str());
    ImGui::EndGroup();
}

/**
 * @brief   Shows plain-language help while the preceding control is hovered.
 * @param[in] text  Tooltip text, wrapped at 24 font sizes.
 */
void Hint(const char *text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

/**
 * @brief   Draws a "?" button beside the preceding control that opens a panel explaining the setting.
 * @param[in] topic            The explanation to show.
 * @param[in] live             Optional extra line computed from the current settings.
 * @param[in] inside_disabled  True when called between BeginDisabled() and EndDisabled(); the button then stays
 *                             enabled, so the explanation is readable while its control is greyed out.
 */
void HelpButton(const HelpTopic &topic, const std::string &live = {}, bool inside_disabled = false) {
    if (inside_disabled) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::PushID(topic.Id.data(), topic.Id.data() + topic.Id.size());
    if (ImGui::SmallButton("?")) {
        ImGui::OpenPopup("help");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip("What is this? Click for an explanation.");
    }
    if (ImGui::BeginPopup("help")) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.f);
        ImGui::TextColored(ImVec4(.55f, .75f, 1.f, 1.f), "%.*s", static_cast<int>(topic.Title.size()),
                           topic.Title.data());
        ImGui::Separator();
        ImGui::TextUnformatted(topic.Body.data(), topic.Body.data() + topic.Body.size());
        if (!live.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted(live.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    if (inside_disabled) {
        ImGui::BeginDisabled();
    }
}

/**
 * @brief   Formats one number with a printf format.
 * @param[in] format  printf format with a single floating-point conversion.
 * @param[in] value   The number.
 * @return  The formatted text, truncated to 47 characters.
 */
std::string FormatNumber(const char *format, double value) {
    char text[48];
    std::snprintf(text, sizeof text, format, value);
    return text;
}

Core::EnergyRatios ComputeEnergyRatios(const Core::GenerationConfig &config) {
    return Core::SnrToEnergyRatios(config.Awgn.SnrDb, std::max(config.SamplesPerSymbol, 1),
                                   std::max(Core::BitsPerSymbol(config.Modulation), 1));
}

/**
 * @brief   Checks the settings without generating.
 * @param[in] config  Settings to check.
 * @return  The validation error, or an empty string when the settings are valid.
 */
std::string ValidationError(const Core::GenerationConfig &config) {
    try {
        Core::Validate(config);
    } catch (const std::exception &error) {
        return error.what();
    }
    return {};
}

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
 * @brief   Creates the window with initial settings.
 * @param[in] config  Initial settings; explicit bits beyond Core::MaxExplicitBits are not shown in the bit editor.
 */
SignalGenerator::SignalGenerator(Core::GenerationConfig config)
    : ImGuiWindowLayer("Signal Generator"), m_Config(std::move(config)), m_BitInput(Core::MaxExplicitBits + 1, 0) {
    const auto count = std::min(m_Config.Bits.size(), m_BitInput.size() - 1);
    std::copy_n(m_Config.Bits.begin(), count, m_BitInput.begin());
}

/**
 * @brief   Draws the settings on the left and the result of the last generation on the right.
 * @note    Picks up a generation that finished since the last frame before drawing.
 */
void SignalGenerator::DrawContents() {
    PollFinishedJob();
    const bool two_pane = ImGui::BeginTable(
        "layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp);
    if (two_pane) {
        ImGui::TableSetupColumn("controls", ImGuiTableColumnFlags_WidthStretch, .36f);
        ImGui::TableSetupColumn("results", ImGuiTableColumnFlags_WidthStretch, .64f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
    }
    DrawControls();
    if (two_pane) {
        ImGui::TableNextColumn();
    }
    DrawSummary();
    if (two_pane) {
        ImGui::EndTable();
    }
}

/**
 * @brief   Takes the result of a finished generation and recomputes every analysis shown for it.
 * @note    A failure is logged and shown in the window instead of propagating.
 */
void SignalGenerator::PollFinishedJob() {
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
 * @brief   Recomputes plot data, measurements, eye diagram, pipeline stages and spectrum, and refits the plots.
 * @param[in] result  The newly generated signal.
 */
void SignalGenerator::RefreshAnalysis(const Core::GeneratedSignal &result) {
    // Cleared first so that a failure below leaves no analysis of the previous signal on screen
    m_Plots = {};
    m_Spectrum = {};
    m_SpectrumDb.clear();
    m_WaveformFit = true;
    m_SpectrumFit = true;
    m_Plots = Core::MakePlotData(result);
    m_Power = Core::MeasurePowerStatistics(result.Samples);
    m_Accuracy = Core::MeasureSymbolAccuracy(result);
    m_Eye = result.Family == Core::Family::Linear ? Core::BuildEyeDiagram(result) : Core::EyeDiagram{};
    m_Pipeline.reset();
    if (result.Family == Core::Family::Linear) {
        m_Pipeline = Core::BuildPipelineStages(result);
    }
    m_PipelineFirst = 0;
    m_PipelineFit = true;
    UpdateSpectrum();
}

/**
 * @brief   Recomputes the Welch PSD of the last result with the selected window, in dB.
 */
void SignalGenerator::UpdateSpectrum() {
    const auto &result = m_Job.Result();
    if (!result) {
        return;
    }
    m_Spectrum = Core::WelchPsd(result->Samples, result->SampleRateHz, 1024, m_Window);
    m_SpectrumDb.resize(m_Spectrum.PowerDensity.size());
    std::transform(m_Spectrum.PowerDensity.begin(), m_Spectrum.PowerDensity.end(), m_SpectrumDb.begin(),
                   [](double power) { return 10 * std::log10(std::max(power, 1e-20)); });
    m_SpectrumFit = true;
}

/**
 * @brief   Copies the explicit bits of the settings into the bit editor, clearing what was there.
 */
void SignalGenerator::ResetBitInput() {
    std::fill(m_BitInput.begin(), m_BitInput.end(), 0);
    std::copy(m_Config.Bits.begin(), m_Config.Bits.end(), m_BitInput.begin());
}

void SignalGenerator::DrawControls() {
    ImGui::PushItemWidth(std::clamp(ImGui::GetContentRegionAvail().x * .5f, 150.f, 360.f));
    ImGui::SeparatorText("Signal Setup");
    DrawModulationSelector();
    const auto family = Core::WaveformFamily(m_Config.Modulation);
    if (family == Core::Family::Noise) {
        DrawNoiseSourceControls();
    } else {
        DrawLinearControls(family == Core::Family::Fsk);
    }
    const std::string validation = ValidationError(m_Config);
    if (!validation.empty()) {
        ImGui::TextColored(ErrorColor, "Invalid settings: %s", validation.c_str());
    }
    ImGui::PopItemWidth();
    DrawActions(validation.empty());
    DrawPresets();
}

void SignalGenerator::DrawModulationSelector() {
    if (ImGui::BeginCombo("Modulation", Core::ModulationName(m_Config.Modulation))) {
        for (const auto &waveform : Core::Waveforms()) {
            const bool selected = waveform.Modulation == m_Config.Modulation;
            if (ImGui::Selectable(waveform.CanonicalName.data(), selected)) {
                Core::SelectWaveform(m_Config, waveform.Modulation);
                ResetBitInput();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    Hint("Linear: BPSK, QPSK, 8-PSK, 16/32/64/256-QAM, OOK, 4-PAM and 4-ASK carry 1 to 8 bits per symbol with Gray "
         "labelling (32-QAM is the cross constellation, only partly Gray); DBPSK, DQPSK, pi/4-DQPSK and 8-DPSK encode "
         "the data in phase changes between symbols; OQPSK delays the quadrature stream by half a symbol. 2-FSK, 4-FSK "
         "and MSK switch the carrier frequency with continuous phase. WGN is a pure noise source.");
}

void SignalGenerator::DrawNoiseSourceControls() {
    ImGui::InputInt("Sample count", &m_Config.NoiseSource.SampleCount);
    Hint("Number of complex noise samples to generate.");
    ImGui::InputDouble("Sample rate (Hz)", &m_Config.NoiseSource.SampleRateHz, 100, 1000, "%.6g");
    Hint("Sets the frequency axis of the spectrum: the PSD spans plus/minus half this rate.");
    ImGui::InputDouble("Noise power", &m_Config.NoiseSource.NoisePower, .1, 1, "%.6g");
    Hint("Total complex noise power. Each of I and Q has half of it as variance.");
    ImGui::TextWrapped("Complex WGN: independent Gaussian I/Q components, each with variance power/2 before gain.");
    ImGui::InputScalar("Noise seed", ImGuiDataType_U32, &m_Config.NoiseSeed);
    ImGui::InputDouble("Amplitude gain", &m_Config.AmplitudeGain, .1, 1, "%.6g");
    Hint("Scales every sample once. Power scales with gain squared.");
}

/**
 * @brief   Draws the settings of symbol-based waveforms: timing, modulation-specific shaping, noise, impairments,
 *          seeds and data source.
 * @param[in] fsk  True for the frequency-shift family, which has tone settings instead of a pulse filter.
 */
void SignalGenerator::DrawLinearControls(bool fsk) {
    ImGui::InputInt("Symbol count", &m_Config.SymbolCount);
    Hint("How many symbols to transmit. Each symbol carries log2(M) bits: 1 for BPSK, OOK, DBPSK, 2-FSK and MSK, 2 "
         "for QPSK, OQPSK, 4-PAM, 4-ASK, DQPSK, pi/4-DQPSK and 4-FSK, 3 for 8-PSK and 8-DPSK, 4 for 16-QAM, 5 for "
         "32-QAM, 6 for 64-QAM, 8 for 256-QAM.");
    ImGui::InputDouble("Symbol rate (Bd)", &m_Config.SymbolRateBaud, 100, 1000, "%.6g");
    Hint("Symbols per second. Together with samples per symbol it sets the sample rate.");
    HelpButton(SymbolRateHelp);
    ImGui::InputInt("Samples per symbol", &m_Config.SamplesPerSymbol);
    Hint("Oversampling factor (SPS). Higher values give smoother waveforms and more room in the spectrum, at the "
         "cost of more samples. RRC needs at least 2; FSK needs enough SPS to keep every tone below half the "
         "sample rate.");
    HelpButton(SPSHelp);
    ImGui::Text("Sample rate: %.6g Hz", m_Config.SymbolRateBaud * m_Config.SamplesPerSymbol);
    ImGui::InputDouble("Amplitude gain", &m_Config.AmplitudeGain, .1, 1, "%.6g");
    Hint("Scales every sample once. Power scales with gain squared.");
    if (fsk) {
        DrawFrequencyModulationControls();
    } else {
        DrawPulseShapingControls();
    }
    DrawAwgnControls();
    DrawImpairmentControls();
    DrawSeedControls();
    DrawAdvancedControls();
}

void SignalGenerator::DrawFrequencyModulationControls() {
    const bool locked = m_Config.Modulation == Core::Modulation::MSK;
    ImGui::SeparatorText("Frequency Modulation");
    double spacing = Core::FskToneSpacingHz(m_Config);
    ImGui::BeginDisabled(locked);
    if (ImGui::InputDouble("Tone spacing (Hz)", &spacing, 50, 500, "%.6g")) {
        m_Config.ToneSpacingHz = spacing;
    }
    ImGui::EndDisabled();
    Hint("Frequency distance between adjacent tones. The modulation index is h = spacing / symbol rate; MSK "
         "fixes it at 0.5, the smallest spacing whose tones stay orthogonal over one symbol.");
    ImGui::Text("Modulation index h: %.4g%s", Core::FskModulationIndex(m_Config), locked ? " (fixed for MSK)" : "");
    ImGui::TextWrapped(
        "Continuous phase, constant envelope. No pulse filter: output is exactly symbols x SPS samples.");
}

void SignalGenerator::DrawPulseShapingControls() {
    ImGui::SeparatorText("Pulse Shaping");
    int pulse = static_cast<int>(m_Config.Pulse);
    if (ImGui::Combo("Pulse", &pulse, "Root-raised cosine\0Rectangular\0")) {
        m_Config.Pulse = static_cast<Core::Pulse>(pulse);
    }
    Hint("RRC is the practical choice: with a matching receive filter it has no inter-symbol interference at "
         "the decision instants and a compact spectrum. Rectangular pulses open the eye fully but have wide "
         "sinc sidelobes.");
    HelpButton(PulseHelp);
    ImGui::BeginDisabled(m_Config.Pulse != Core::Pulse::RRC);
    ImGui::InputDouble("RRC roll-off", &m_Config.RollOff, .05, .1, "%.4g");
    Hint("Excess bandwidth of the root-raised-cosine filter, from 0 to 1. Occupied bandwidth is about symbol "
         "rate x (1 + roll-off). Low values give narrow spectra but longer, more sensitive filter tails.");
    HelpButton(RollOffHelp, {}, m_Config.Pulse != Core::Pulse::RRC);
    ImGui::InputInt("RRC span (symbols)", &m_Config.SpanSymbols);
    Hint("Filter length in symbols. Longer filters approximate the ideal response better (less residual ISI) "
         "and cost more samples.");
    HelpButton(SpanHelp, {}, m_Config.Pulse != Core::Pulse::RRC);
    ImGui::EndDisabled();
}

void SignalGenerator::DrawAwgnControls() {
    ImGui::SeparatorText("AWGN");
    ImGui::Checkbox("Add AWGN", &m_Config.Awgn.Enabled);
    Hint("Additive white Gaussian noise at the SNR below. Disabled keeps the clean signal.");
    ImGui::BeginDisabled(!m_Config.Awgn.Enabled);
    ImGui::InputDouble("SNR (dB)", &m_Config.Awgn.SnrDb, 1, 10, "%.6g");
    Hint("Clean signal power divided by added noise power, in dB. Lower values spread the constellation and close "
         "the eye.");
    {
        const auto ratios = ComputeEnergyRatios(m_Config);
        HelpButton(SnrHelp,
                   FormatNumber("With your settings: SNR %.4g dB", m_Config.Awgn.SnrDb) +
                       FormatNumber(" = Es/N0 %.4g dB", ratios.EsN0Db) +
                       FormatNumber(" = Eb/N0 %.4g dB", ratios.EbN0Db),
                   !m_Config.Awgn.Enabled);
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("SNR is clean sample power over added complex noise power, measured on the steady-state "
                       "interval. Es/N0 = SNR + 10 log10(SPS); Eb/N0 = Es/N0 - 10 log10(bits per symbol).");
    if (m_Config.Awgn.Enabled && std::isfinite(m_Config.Awgn.SnrDb)) {
        const auto ratios = ComputeEnergyRatios(m_Config);
        ImGui::Text("Equivalent: Es/N0 %.4g dB | Eb/N0 %.4g dB", ratios.EsN0Db, ratios.EbN0Db);
    }
}

void SignalGenerator::DrawImpairmentControls() {
    ImGui::SeparatorText("Channel impairments");
    auto &impairments = m_Config.Impairments;
    ImGui::InputDouble("CFO (Hz)", &impairments.CfoHz, 1, 10, "%.6g");
    Hint("Carrier frequency offset. The constellation spins at this rate; a receiver needs carrier recovery to "
         "stop it.");
    ImGui::InputDouble("Phase noise linewidth (Hz)", &impairments.PhaseNoiseLinewidthHz, 0.1, 1, "%.6g");
    Hint("3 dB linewidth of a free-running oscillator. The phase random-walks, smearing each constellation point "
         "into an arc.");
    ImGui::InputDouble("IQ gain imbalance (dB)", &impairments.IqGainDb, 0.1, 1, "%.6g");
    Hint("Gain of the Q branch relative to I. Squeezes the constellation along one axis and leaves an image of the "
         "signal in the spectrum.");
    ImGui::InputDouble("IQ phase skew (deg)", &impairments.IqPhaseDeg, 0.5, 5, "%.6g");
    Hint("Quadrature error: the Q branch leaks a little of I, shearing the constellation.");
    ImGui::InputDouble("DC offset I (x RMS)", &impairments.DcOffsetI, 0.01, 0.1, "%.6g");
    ImGui::InputDouble("DC offset Q (x RMS)", &impairments.DcOffsetQ, 0.01, 0.1, "%.6g");
    Hint("A constant added to I or Q, as a fraction of the signal RMS amplitude. Shifts the whole constellation "
         "and adds a spectral line at 0 Hz.");
    ImGui::InputInt("ADC bits", &impairments.AdcBits);
    Hint("Quantizer resolution per component, 2 to 24; 0 disables it. The full scale auto-ranges to the largest I "
         "or Q magnitude. Few bits give a staircase waveform and a grid-like constellation.");
    ImGui::TextWrapped("Applied after AWGN, in the order: CFO, phase noise, IQ imbalance, DC offset, quantization.");
    ImGui::InputScalar("Impairment seed", ImGuiDataType_U32, &m_Config.ImpairmentSeed);
    Hint("Seeds the phase-noise random walk; it is independent of the data and AWGN seeds.");
}

void SignalGenerator::DrawSeedControls() {
    ImGui::InputScalar("Noise seed", ImGuiDataType_U32, &m_Config.NoiseSeed);
    ImGui::InputScalar("Random seed", ImGuiDataType_U32, &m_Config.Seed);
    Hint("Same settings and seeds always reproduce the same bits and noise. The data and noise seeds are "
         "independent streams.");
}

/**
 * @brief   Draws the collapsible data-source settings, with the bit editor for explicit bits.
 */
void SignalGenerator::DrawAdvancedControls() {
    if (!ImGui::CollapsingHeader("Advanced")) {
        return;
    }
    int source = static_cast<int>(m_Config.DataSource);
    if (ImGui::Combo("Data source", &source, "Seeded random bits\0Explicit bits\0")) {
        m_Config.DataSource = static_cast<Core::DataSource>(source);
    }
    if (m_Config.DataSource == Core::DataSource::Explicit) {
        if (ImGui::InputTextMultiline("Bits", m_BitInput.data(), m_BitInput.size(), ImVec2(-1, 70))) {
            m_Config.Bits = m_BitInput.data();
        }
        ImGui::TextUnformatted("Enter exactly symbol count x bits per symbol digits (0 or 1).");
    }
}

/**
 * @brief   Draws the Generate and Export buttons, the export dialogs, the progress note and the last error.
 * @param[in] settings_valid  False disables Generate.
 */
void SignalGenerator::DrawActions(bool settings_valid) {
    ImGui::Separator();
    ImGui::BeginDisabled(m_Job.Busy() || !settings_valid);
    if (ImGui::Button("Generate Signal")) {
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
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_Job.Result());
    if (ImGui::Button("Export I/Q...")) {
        m_ExportResult = m_Job.Result();
        m_ExportStatus.clear();
        m_ConfirmOverwrite = false;
        ImGui::OpenPopup("Export I/Q");
    }
    ImGui::EndDisabled();
    DrawExportDialog();
    DrawImageExportDialog();
    if (m_Job.Busy()) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Generating...");
    }
    if (!m_Error.empty()) {
        ImGui::TextColored(ErrorColor, "Operation failed: %s", m_Error.c_str());
    }
}

/**
 * @brief   Draws the collapsible preset save/load controls and the notes of the last loaded preset.
 */
void SignalGenerator::DrawPresets() {
    if (ImGui::CollapsingHeader("Presets")) {
        ImGui::InputText("Preset path", m_PresetPath, sizeof m_PresetPath);
        Hint("Guided lessons ship in the presets/ folder, for example presets/01-qpsk-clean.preset. Loading one shows "
             "its notes below.");
        if (ImGui::Button("Save Preset")) {
            try {
                Core::SavePreset(m_PresetPath, m_Config);
                spdlog::info("Saved preset: {}", m_PresetPath);
                m_Error.clear();
            } catch (const std::exception &error) {
                spdlog::error("Preset save failed: {}", error.what());
                m_Error = error.what();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Preset")) {
            try {
                auto loaded = Core::LoadPreset(m_PresetPath);
                m_Config = std::move(loaded);
                m_PresetNotes = Core::LoadPresetNotes(m_PresetPath);
                spdlog::info("Loaded preset: {}", m_PresetPath);
                ResetBitInput();
                m_Error.clear();
            } catch (const std::exception &error) {
                spdlog::error("Preset load failed: {}", error.what());
                m_Error = error.what();
            }
        }
    }
    if (!m_PresetNotes.empty()) {
        ImGui::SeparatorText("Preset notes");
        ImGui::TextWrapped("%s", m_PresetNotes.c_str());
    }
}

/**
 * @brief   Draws the summary, measurements and plots of the last result, warning when the settings changed since.
 */
void SignalGenerator::DrawSummary() {
    ImGui::SeparatorText("Signal Summary");
    const auto &result = m_Job.Result();
    if (!result) {
        ImGui::TextDisabled("No signal generated yet. Choose settings and select Generate Signal.");
        return;
    }
    if (result->Config != m_Config) {
        ImGui::TextColored(ImVec4(.95f, .75f, .30f, 1.f), "Settings changed. Generate to update the displayed result.");
    }
    if (result->Family == Core::Family::Noise) {
        DrawNoiseSummary(*result);
    } else {
        DrawSymbolSummary(*result);
    }
    DrawMeasurements(*result);
    DrawPlots();
}

void SignalGenerator::DrawNoiseSummary(const Core::GeneratedSignal &result) {
    double total = 0;
    for (const auto &sample : result.Samples) {
        total += std::norm(sample);
    }
    const auto mean_power = result.Samples.empty() ? 0 : total / static_cast<double>(result.Samples.size());
    Metric("Signal", std::string(Core::ModulationName(result.Config.Modulation)), true);
    Metric("Samples", std::to_string(result.Samples.size()));
    Metric("Sample rate", FormatNumber("%.6g Hz", result.SampleRateHz));
    Metric("Duration", FormatNumber("%.6g s", result.Samples.size() / result.SampleRateHz));
    Metric("Noise power (set)", FormatNumber("%.6g", result.Config.NoiseSource.NoisePower), true);
    Metric("Mean power (measured)", FormatNumber("%.6g", mean_power));
    Metric("Gain", FormatNumber("%.6g", result.Config.AmplitudeGain));
    Metric("Noise seed", std::to_string(result.Noise.NoiseSeed));
}

void SignalGenerator::DrawSymbolSummary(const Core::GeneratedSignal &result) {
    Metric("Signal", std::string(Core::ModulationName(result.Config.Modulation)), true);
    Metric("Symbols", std::to_string(result.Family == Core::Family::Fsk ? result.SymbolFrequenciesHz.size()
                                                                        : result.Symbols.size()));
    Metric("Samples", std::to_string(result.Samples.size()));
    Metric("Sample rate", FormatNumber("%.6g Hz", result.SampleRateHz));
    Metric("Duration", FormatNumber("%.6g s", result.Samples.size() / result.SampleRateHz));
    if (result.Family == Core::Family::Fsk) {
        Metric("Modulation index", FormatNumber("%.4g", Core::FskModulationIndex(result.Config)), true);
    } else {
        Metric("Filter delay", std::to_string(result.FilterDelaySamples) + " samples", true);
    }
    Metric("Gain", FormatNumber("%.6g", result.Config.AmplitudeGain));
    if (result.Noise.AwgnApplied) {
        ImGui::TextWrapped("AWGN: requested %.6g dB | reference power %.6g over [%zu,%zu) | added noise power "
                           "%.6g | noise seed %u",
                           result.Noise.RequestedSnrDb, result.Noise.ReferencePower, result.Noise.ReferenceBegin,
                           result.Noise.ReferenceEnd, result.Noise.AddedNoisePower, result.Noise.NoiseSeed);
    }
    if (result.ImpairmentsApplied) {
        const auto &impairments = result.Config.Impairments;
        ImGui::TextWrapped("Impairments: CFO %.6g Hz | phase noise %.6g Hz | IQ %.6g dB / %.6g deg | DC "
                           "%.6g%+.6gj x RMS | ADC %d bits | seed %u",
                           impairments.CfoHz, impairments.PhaseNoiseLinewidthHz, impairments.IqGainDb,
                           impairments.IqPhaseDeg, impairments.DcOffsetI, impairments.DcOffsetQ, impairments.AdcBits,
                           result.Config.ImpairmentSeed);
    }
}

void SignalGenerator::DrawMeasurements(const Core::GeneratedSignal &result) {
    ImGui::SeparatorText("Measurements");
    Metric("Mean power", FormatNumber("%.4g", m_Power.MeanPower), true);
    Hint("Mean of |x|^2 over every sample, filter transients included.");
    Metric("Peak power", FormatNumber("%.4g", m_Power.PeakPower));
    Metric("PAPR", FormatNumber("%.3g dB", m_Power.PaprDb));
    Hint("Peak-to-average power ratio. A constant-envelope signal has 0 dB; shaped QAM is several dB higher, which is "
         "what stresses power amplifiers.");
    if (m_Accuracy) {
        Metric("EVM", FormatNumber("%.3g %%", 100 * m_Accuracy->EvmRms), true);
        Hint("RMS error between the matched-filter observations and the ideal symbols, relative to the RMS ideal "
             "symbol. Even a clean RRC signal shows a small floor from the truncated filter.");
        Metric("EVM", FormatNumber("%.4g dB", m_Accuracy->EvmDb));
        Metric("SNR after matched filter", FormatNumber("%.4g dB", m_Accuracy->SnrAfterMatchedDb));
        Hint("-EVM in dB. Matched filtering averages noise over about SPS samples, so this exceeds the sample-level "
             "SNR by 10 log10(SPS).");
        if (result.Noise.AwgnApplied) {
            ImGui::TextWrapped("Requested sample SNR %.4g dB + 10 log10(SPS) = %.4g dB expected after the matched "
                               "filter. The measured value varies with the noise realization.",
                               result.Noise.RequestedSnrDb, result.Noise.RequestedSnrDb + m_Accuracy->ExpectedOffsetDb);
        }
    }
}

/**
 * @brief   Draws the tab bar of plots; each tab appears only for the waveform families it applies to.
 */
void SignalGenerator::DrawPlots() {
    if (!ImGui::BeginTabBar("Signal views")) {
        return;
    }
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(.15f, .15f));
    DrawWaveformTab();
    const auto &result = m_Job.Result();
    const bool noise_source = result && result->Family == Core::Family::Noise;
    const bool fsk_source = result && result->Family == Core::Family::Fsk;
    const bool symbol_views = !noise_source && !fsk_source;
    if (fsk_source) {
        DrawFrequencyTab();
    }
    if (symbol_views) {
        DrawConstellationTab(result && (result->Noise.AwgnApplied || result->ImpairmentsApplied));
        DrawEyeTab();
    }
    if (symbol_views && result && m_Pipeline) {
        DrawPipelineTab(*result);
    }
    DrawSpectrumTab();
    ImPlot::PopStyleVar();
    ImGui::EndTabBar();
}

void SignalGenerator::DrawWaveformTab() {
    if (!ImGui::BeginTabItem("Waveform")) {
        return;
    }
    ExportImageButton("waveform", [&] { return Core::WaveformFigure(m_Plots); });
    ImGui::Text("I: blue | Q: orange | %zu plotted points (min/max reduction)", m_Plots.Time.size());
    if (m_WaveformFit) {
        ImPlot::SetNextAxesToFit();
    }
    if (ImPlot::BeginPlot("Complex baseband", ImVec2(-1, -1))) {
        m_WaveformFit = false;
        ImPlot::SetupAxes("Time (s)", "Amplitude");
        ImPlot::SetNextLineStyle(InPhaseColor);
        ImPlot::PlotLine("I", m_Plots.Time.data(), m_Plots.I.data(), static_cast<int>(m_Plots.Time.size()));
        ImPlot::SetNextLineStyle(QuadratureColor);
        ImPlot::PlotLine("Q", m_Plots.Time.data(), m_Plots.Q.data(), static_cast<int>(m_Plots.Time.size()));
        ImPlot::EndPlot();
    }
    ImGui::EndTabItem();
}

void SignalGenerator::DrawFrequencyTab() {
    if (!ImGui::BeginTabItem("Frequency")) {
        return;
    }
    ExportImageButton("frequency", [&] { return Core::FrequencyFigure(m_Plots); });
    ImGui::TextWrapped("Blue: frequency estimated from the phase step between consecutive samples. Orange: nominal "
                       "tone of each symbol. A continuous-phase signal moves between tones without phase jumps; "
                       "noise and CFO shift the estimate.");
    if (ImPlot::BeginPlot("Instantaneous frequency", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Time (s)", "Frequency (Hz)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetNextLineStyle(InPhaseColor);
        ImPlot::PlotLine("Estimate", m_Plots.FreqTime.data(), m_Plots.FreqEstimate.data(),
                         static_cast<int>(m_Plots.FreqTime.size()));
        ImPlot::SetNextLineStyle(QuadratureColor, 2.f);
        ImPlot::PlotLine("Nominal tone", m_Plots.FreqTime.data(), m_Plots.FreqNominal.data(),
                         static_cast<int>(m_Plots.FreqTime.size()));
        ImPlot::EndPlot();
    }
    ImGui::EndTabItem();
}

/**
 * @brief   Draws the constellation tab, with the mapped symbols or the matched-filter observations.
 * @param[in] noisy  True when AWGN or impairments were applied; only changes the labels of the view selector.
 */
void SignalGenerator::DrawConstellationTab(bool noisy) {
    if (!ImGui::BeginTabItem("Constellation")) {
        return;
    }
    ImGui::Combo("View", &m_ConstellationView,
                 noisy ? "Mapped symbols (ideal, gain applied)\0Matched filter (degraded observations)\0"
                       : "Mapped symbols (gain applied)\0Matched filter (steady-state symbols)\0");
    const auto &data = m_ConstellationView == 0 ? m_Plots.Mapped : m_Plots.Matched;
    if (data.X.empty()) {
        ImGui::TextWrapped("No steady-state symbols: increase symbol count beyond twice the RRC span.");
    } else {
        ExportImageButton("constellation", [&] {
            return Core::ConstellationFigure(data, m_ConstellationView == 0 ? "I/Q constellation (mapped symbols)"
                                                                            : "I/Q constellation (matched filter)");
        });
    }
    if (ImPlot::BeginPlot("I/Q constellation", ImVec2(-1, -1), ImPlotFlags_Equal)) {
        ImPlot::SetupAxes("In-phase (I)", "Quadrature (Q)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3, InPhaseColor);
        ImPlot::PlotScatter("Symbols", data.X.data(), data.Y.data(), static_cast<int>(data.X.size()));
        ImPlot::EndPlot();
    }
    ImGui::EndTabItem();
}

void SignalGenerator::DrawEyeTab() {
    if (!ImGui::BeginTabItem("Eye")) {
        return;
    }
    ImGui::SetNextItemWidth(220);
    ImGui::Combo("Component", &m_EyeComponent, "In-phase (I)\0Quadrature (Q)\0");
    if (m_Eye.InPhase.empty()) {
        ImGui::TextWrapped("No steady-state symbols: increase symbol count beyond twice the RRC span.");
    } else {
        ImGui::SameLine();
        ExportImageButton("eye", [&] { return Core::EyeFigure(m_Eye, m_EyeComponent == 1); });
        ImGui::TextWrapped("%zu overlaid matched-filter traces, two symbol periods wide. A wide-open eye at 0 "
                           "means easy, error-free decisions; noise and ISI close it.",
                           m_Eye.InPhase.size());
    }
    if (ImPlot::BeginPlot("Eye diagram", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Time (symbol periods)", m_EyeComponent == 0 ? "I" : "Q", ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_AutoFit);
        const auto &traces = m_EyeComponent == 0 ? m_Eye.InPhase : m_Eye.Quadrature;
        const auto colour = m_EyeComponent == 0 ? ImVec4(.2f, .6f, 1.f, .35f) : ImVec4(1.f, .55f, .15f, .35f);
        for (const auto &trace : traces) {
            ImPlot::SetNextLineStyle(colour);
            ImPlot::PlotLine("##eye", m_Eye.TimeSymbols.data(), trace.data(), static_cast<int>(trace.size()));
        }
        ImPlot::EndPlot();
    }
    ImGui::EndTabItem();
}

void SignalGenerator::DrawPipelineTab(const Core::GeneratedSignal &result) {
    if (!ImGui::BeginTabItem("Pipeline")) {
        return;
    }
    DrawPipelineControls();
    DrawPipelineStages(result);
    ImGui::EndTabItem();
}

/**
 * @brief   Draws the export button, explanation and window sliders of the pipeline view.
 * @note    Clamps the window to the available symbols and requests a refit when the user moves it.
 */
void SignalGenerator::DrawPipelineControls() {
    const auto &stages = *m_Pipeline;
    const int total = static_cast<int>(stages.Symbols.size());
    ExportImageButton("pipeline",
                      [&] { return Core::PipelineFigure(stages, m_PipelineFirst, m_PipelineCount, m_PipelineAlign); });
    ImGui::TextWrapped("One transmission, step by step, on a shared time axis (symbol periods). I: blue | Q: orange");
    HelpButton(PipelineHelp);
    bool moved = false;
    ImGui::SetNextItemWidth(220);
    m_PipelineCount = std::clamp(m_PipelineCount, std::min(4, total), std::min(64, total));
    moved |= ImGui::SliderInt("Symbols shown", &m_PipelineCount, std::min(4, total), std::min(64, total));
    Hint("How many symbols the rows cover. Fewer symbols make each step easier to read.");
    const int max_first = std::max(0, total - m_PipelineCount);
    m_PipelineFirst = std::clamp(m_PipelineFirst, 0, max_first);
    ImGui::SetNextItemWidth(220);
    moved |= ImGui::SliderInt("First symbol", &m_PipelineFirst, 0, max_first);
    Hint("Index of the first symbol shown. Scroll through the signal; the start and end show the filter ramping up and "
         "down.");
    ImGui::BeginDisabled(stages.FilterDelaySamples == 0);
    ImGui::Checkbox("Compensate filter delay", &m_PipelineAlign);
    ImGui::EndDisabled();
    Hint("The pulse filter delays its output by half its span. With this on, steps 4 and 5 are shifted back so each "
         "pulse peak lines up with its symbol in steps 2 and 3.");
    if (stages.FilterDelaySamples > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(delay %.4g symbols)",
                            static_cast<double>(stages.FilterDelaySamples) / stages.SamplesPerSymbol);
    }
    if (moved) {
        m_PipelineFit = true;
    }
}

/**
 * @brief   Draws the five pipeline rows over the selected window of symbols, on a shared time axis.
 * @param[in] result  The signal the stages were built from; gives the pulse name of row 4.
 */
void SignalGenerator::DrawPipelineStages(const Core::GeneratedSignal &result) {
    const auto &stages = *m_Pipeline;
    const int sps = stages.SamplesPerSymbol;
    const int bits_per_symbol = stages.BitsPerSymbol;
    const int count = m_PipelineCount;
    const int first = m_PipelineFirst;
    const bool align = m_PipelineAlign && stages.FilterDelaySamples > 0;
    const auto offset = align ? stages.FilterDelaySamples : 0;
    struct Series {
        std::vector<double> X, I, Q;
    };
    // Samples [first*SPS, (first+count)*SPS) of a stage, with x in symbol periods
    auto window_of = [&](const std::vector<std::complex<float>> &stage, std::size_t shift, bool closed) {
        Series series;
        const auto begin = static_cast<std::size_t>(first) * static_cast<std::size_t>(sps) + shift;
        const auto length = static_cast<std::size_t>(count) * static_cast<std::size_t>(sps) + (closed ? 1 : 0);
        for (std::size_t k = 0; k < length && begin + k < stage.size(); ++k) {
            series.X.push_back(first + static_cast<double>(k) / sps);
            series.I.push_back(stage[begin + k].real());
            series.Q.push_back(stage[begin + k].imag());
        }
        return series;
    };
    auto start_plot = [&](const char *title, bool last, double y_min, double y_max) {
        if (!ImPlot::BeginPlot(title, ImVec2(0, 0), ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
            return false;
        }
        ImPlot::SetupAxes(last ? "Time (symbol periods)" : nullptr, nullptr, 0, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, first, first + count, m_PipelineFit ? ImPlotCond_Always : ImPlotCond_Once);
        if (y_min < y_max) {
            ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImPlotCond_Always);
        }
        return true;
    };
    // Shared vertical scale for the sample-level rows so that amplitude changes between steps are visible
    double peak = 1e-9;
    for (const auto *stage : {&stages.Symbols, &stages.Upsampled, &stages.Shaped, &stages.Received}) {
        for (const auto &sample : *stage) {
            peak = std::max(
                {peak, static_cast<double>(std::abs(sample.real())), static_cast<double>(std::abs(sample.imag()))});
        }
    }
    const double y_limit = peak * 1.25;
    if (ImPlot::BeginSubplots("##pipeline", 5, 1, ImVec2(-1, -1),
                              ImPlotSubplotFlags_LinkAllX | ImPlotSubplotFlags_NoTitle)) {
        if (start_plot("1. Data bits", false, -.3, 1.3)) {
            std::vector<double> x, y;
            const int bit_begin = first * bits_per_symbol;
            const int bit_end = std::min(static_cast<int>(stages.Bits.size()), (first + count) * bits_per_symbol);
            for (int b = bit_begin; b < bit_end; ++b) {
                x.push_back(static_cast<double>(b) / bits_per_symbol);
                y.push_back(stages.Bits[static_cast<std::size_t>(b)] == '1' ? 1. : 0.);
            }
            if (!x.empty()) {
                x.push_back(static_cast<double>(bit_end) / bits_per_symbol);
                y.push_back(y.back());
                ImPlot::SetNextLineStyle(InPhaseColor, 2.f);
                ImPlot::PlotStairs("bits", x.data(), y.data(), static_cast<int>(x.size()));
                if (bit_end - bit_begin <= 96) {
                    for (int b = bit_begin; b < bit_end; ++b) {
                        ImPlot::PlotText(stages.Bits[static_cast<std::size_t>(b)] == '1' ? "1" : "0",
                                         (b + .5) / bits_per_symbol, .5);
                    }
                }
            }
            ImPlot::EndPlot();
        }
        if (start_plot("2. Mapped symbols (one complex value per symbol)", false, -y_limit, y_limit)) {
            std::vector<double> x, yi, yq;
            for (int k = first; k < first + count; ++k) {
                x.push_back(k);
                yi.push_back(stages.Symbols[static_cast<std::size_t>(k)].real());
                yq.push_back(stages.Symbols[static_cast<std::size_t>(k)].imag());
            }
            x.push_back(first + count);
            yi.push_back(yi.back());
            yq.push_back(yq.back());
            ImPlot::SetNextLineStyle(InPhaseColor, 2.f);
            ImPlot::PlotStairs("I", x.data(), yi.data(), static_cast<int>(x.size()));
            ImPlot::SetNextLineStyle(QuadratureColor, 2.f);
            ImPlot::PlotStairs("Q", x.data(), yq.data(), static_cast<int>(x.size()));
            ImPlot::EndPlot();
        }
        if (start_plot("3. Zeros inserted between symbols (upsampling by SPS)", false, -y_limit, y_limit)) {
            const auto series = window_of(stages.Upsampled, 0, false);
            ImPlot::SetNextLineStyle(InPhaseColor, 1.5f);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 2.5f, InPhaseColor);
            ImPlot::PlotStems("I", series.X.data(), series.I.data(), static_cast<int>(series.X.size()));
            ImPlot::SetNextLineStyle(QuadratureColor, 1.5f);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 2.5f, QuadratureColor);
            ImPlot::PlotStems("Q", series.X.data(), series.Q.data(), static_cast<int>(series.X.size()));
            ImPlot::EndPlot();
        }
        const std::string shaped_title =
            std::string("4. After the pulse filter (") +
            (result.Config.Pulse == Core::Pulse::RRC ? "root-raised cosine" : "rectangular") + ")";
        if (start_plot(shaped_title.c_str(), false, -y_limit, y_limit)) {
            const auto series = window_of(stages.Shaped, offset, true);
            ImPlot::SetNextLineStyle(InPhaseColor, 1.5f);
            ImPlot::PlotLine("I", series.X.data(), series.I.data(), static_cast<int>(series.X.size()));
            ImPlot::SetNextLineStyle(QuadratureColor, 1.5f);
            ImPlot::PlotLine("Q", series.X.data(), series.Q.data(), static_cast<int>(series.X.size()));
            ImPlot::EndPlot();
        }
        const char *noisy_title = stages.Degraded
                                      ? "5. With noise and impairments (the exported signal)"
                                      : "5. With noise: none added yet (enable AWGN or an impairment and generate)";
        if (start_plot(noisy_title, true, -y_limit, y_limit)) {
            const auto series = window_of(stages.Received, offset, true);
            ImPlot::SetNextLineStyle(InPhaseColor, 1.5f);
            ImPlot::PlotLine("I", series.X.data(), series.I.data(), static_cast<int>(series.X.size()));
            ImPlot::SetNextLineStyle(QuadratureColor, 1.5f);
            ImPlot::PlotLine("Q", series.X.data(), series.Q.data(), static_cast<int>(series.X.size()));
            ImPlot::EndPlot();
        }
        ImPlot::EndSubplots();
    }
    m_PipelineFit = false;
}

void SignalGenerator::DrawSpectrumTab() {
    if (!ImGui::BeginTabItem("Spectrum")) {
        return;
    }
    int window = static_cast<int>(m_Window);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Window", &window, "Hann\0Hamming\0Blackman\0Rectangular\0")) {
        m_Window = static_cast<Core::Window>(window);
        UpdateSpectrum();
    }
    if (!m_SpectrumDb.empty()) {
        ImGui::SameLine();
        ExportImageButton("spectrum", [&] { return Core::SpectrumFigure(m_Spectrum, m_SpectrumDb); });
    }
    Hint("Hann is the default. Rectangular has the narrowest main lobe but the worst leakage; Blackman has the "
         "lowest sidelobes with a wider main lobe.");
    ImGui::Text("Two-sided Welch PSD | Periodic %s | %zu samples/segment | %zu segments",
                Core::WindowName(m_Spectrum.Window), m_Spectrum.SegmentLength, m_Spectrum.SegmentCount);
    ImGui::TextWrapped("Relative power density; no impedance or watt/dBm calibration. Display floor: -200 dB.");
    if (m_SpectrumDb.empty()) {
        ImGui::TextUnformatted("At least four samples are needed for a spectrum.");
    } else {
        if (m_SpectrumFit) {
            ImPlot::SetNextAxesToFit();
        }
        if (ImPlot::BeginPlot("Baseband PSD", ImVec2(-1, -1))) {
            m_SpectrumFit = false;
            ImPlot::SetupAxes("Frequency (Hz)", "PSD (dB re 1 amplitude^2/Hz)");
            ImPlot::SetNextLineStyle(InPhaseColor);
            ImPlot::PlotLine("I + jQ", m_Spectrum.FrequencyHz.data(), m_SpectrumDb.data(),
                             static_cast<int>(m_SpectrumDb.size()));
            ImPlot::EndPlot();
        }
    }
    ImGui::EndTabItem();
}

/**
 * @brief   Draws the I/Q export dialog once DrawActions() opens it.
 * @note    Exports the result captured when the dialog opened, even if a new generation finishes meanwhile, and
 *          asks before replacing an existing sample or metadata file.
 */
void SignalGenerator::DrawExportDialog() {
    ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Export I/Q", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextWrapped("Export the completed signal captured when this dialog opened.");
    if (ImGui::InputText("Destination", m_ExportPath, sizeof m_ExportPath)) {
        m_ConfirmOverwrite = false;
    }
    if (ImGui::Combo("Format", &m_ExportFormat, "CSV\0Binary float32 I/Q\0")) {
        m_ConfirmOverwrite = false;
    }
    ImGui::TextWrapped("Metadata is written beside the samples as <destination>.json.");
    if (ImGui::Button("Export")) {
        try {
            m_ConfirmOverwrite =
                std::filesystem::exists(m_ExportPath) || std::filesystem::exists(Core::MetadataPath(m_ExportPath));
            if (!m_ConfirmOverwrite) {
                Export(false);
            }
        } catch (const std::exception &error) {
            spdlog::error("Unable to inspect export destination {}: {}", m_ExportPath, error.what());
            m_ExportStatus = error.what();
        }
    }
    if (m_ConfirmOverwrite) {
        ImGui::TextWrapped("The destination or metadata exists. Replace both files?");
        if (ImGui::Button("Confirm overwrite")) {
            Export(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel overwrite")) {
            m_ConfirmOverwrite = false;
        }
    }
    if (!m_ExportStatus.empty()) {
        ImGui::TextWrapped("%s", m_ExportStatus.c_str());
    }
    if (ImGui::Button("Close")) {
        m_ExportResult.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

/**
 * @brief   Writes the captured result and its JSON metadata, and records the outcome for the dialog.
 * @param[in] overwrite  True to replace existing files.
 */
void SignalGenerator::Export(bool overwrite) {
    try {
        Core::ExportSignal(m_ExportPath, *m_ExportResult, static_cast<Core::ExportFormat>(m_ExportFormat), overwrite);
        spdlog::info("Exported {} complex samples to {}", m_ExportResult->Samples.size(), m_ExportPath);
        m_ExportStatus = "Exported samples and JSON metadata.";
        m_ConfirmOverwrite = false;
    } catch (const std::exception &error) {
        spdlog::error("Export to {} failed: {}", m_ExportPath, error.what());
        m_ExportStatus = std::string("Export failed: ") + error.what();
    }
}

/**
 * @brief   Draws an "Export image..." button that captures the plot and opens the image export dialog.
 * @param[in] stem   Plot name used in the suggested file name, siggen-<stem>.png or .svg.
 * @param[in] build  Builds the figure from the plotted data; called only when the button is pressed.
 */
void SignalGenerator::ExportImageButton(const char *stem, const std::function<Core::Figure()> &build) {
    if (ImGui::Button("Export image...")) {
        try {
            m_ImageFigure = build();
            std::snprintf(m_ImagePath, sizeof m_ImagePath, "siggen-%s.%s", stem, m_ImageFormat == 0 ? "png" : "svg");
            m_ImageStatus.clear();
            m_ImageConfirmOverwrite = false;
            m_ImageOpenRequested = true;
        } catch (const std::exception &error) {
            spdlog::error("Unable to prepare image: {}", error.what());
            m_Error = error.what();
        }
    }
    Hint("Save this plot as a PNG or SVG image for slides and reports. The image is drawn from the plotted data, so it "
         "does not depend on the window size.");
}

/**
 * @brief   Draws the image export dialog once ExportImageButton() requests it.
 * @note    The request is deferred to here because the buttons sit inside tab scopes, where opening the popup
 *          would give it an ID that this BeginPopupModal() call does not match.
 */
void SignalGenerator::DrawImageExportDialog() {
    if (m_ImageOpenRequested) {
        ImGui::OpenPopup("Export image");
        m_ImageOpenRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Export image", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextWrapped("Save the plot as an image. PNG is a raster for slides; SVG is vector and stays sharp at any "
                       "size in documents.");
    if (ImGui::InputText("Destination", m_ImagePath, sizeof m_ImagePath)) {
        m_ImageConfirmOverwrite = false;
    }
    if (ImGui::Combo("Format", &m_ImageFormat, "PNG\0SVG\0")) {
        // Keep the name, swap the extension
        auto path = std::filesystem::path(m_ImagePath);
        path.replace_extension(m_ImageFormat == 0 ? ".png" : ".svg");
        std::snprintf(m_ImagePath, sizeof m_ImagePath, "%s", path.string().c_str());
        m_ImageConfirmOverwrite = false;
    }
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Width (px)", &m_ImageWidth, 100, 400);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Height (px)", &m_ImageHeight, 100, 400);
    m_ImageWidth = std::clamp(m_ImageWidth, Core::MinImageSize, Core::MaxImageSize);
    m_ImageHeight = std::clamp(m_ImageHeight, Core::MinImageSize, Core::MaxImageSize);
    ImGui::Combo("Background", &m_ImageTheme, "Dark (matches the app)\0Light (for print)\0");
    if (ImGui::Button("Save image")) {
        try {
            m_ImageConfirmOverwrite = std::filesystem::exists(m_ImagePath);
            if (!m_ImageConfirmOverwrite) {
                ExportImage(false);
            }
        } catch (const std::exception &error) {
            m_ImageStatus = error.what();
        }
    }
    if (m_ImageConfirmOverwrite) {
        ImGui::TextWrapped("The destination exists. Replace it?");
        if (ImGui::Button("Confirm overwrite")) {
            ExportImage(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel overwrite")) {
            m_ImageConfirmOverwrite = false;
        }
    }
    if (!m_ImageStatus.empty()) {
        ImGui::TextWrapped("%s", m_ImageStatus.c_str());
    }
    if (ImGui::Button("Close")) {
        m_ImageFigure.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

/**
 * @brief   Writes the captured figure with the dialog settings, and records the outcome for the dialog.
 * @param[in] overwrite  True to replace an existing file.
 */
void SignalGenerator::ExportImage(bool overwrite) {
    try {
        Core::ImageStyle style;
        style.Width = m_ImageWidth;
        style.Height = m_ImageHeight;
        style.Dark = m_ImageTheme == 0;
        Core::ExportFigure(m_ImagePath, *m_ImageFigure,
                           m_ImageFormat == 0 ? Core::ImageFormat::PNG : Core::ImageFormat::SVG, style, overwrite);
        spdlog::info("Exported image {} ({}x{})", m_ImagePath, style.Width, style.Height);
        m_ImageStatus = std::string("Saved ") + m_ImagePath;
        m_ImageConfirmOverwrite = false;
    } catch (const std::exception &error) {
        spdlog::error("Image export to {} failed: {}", m_ImagePath, error.what());
        m_ImageStatus = std::string("Export failed: ") + error.what();
    }
}

} // namespace GUI
