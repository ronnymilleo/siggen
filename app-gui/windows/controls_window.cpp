/**
 * @file    controls_window.cpp
 * @brief   The Signal Setup window: generation settings, Generate and I/Q export, and presets.
 */

#include "controls_window.h"

#include "help_topics.h"
#include "iq_export.h"
#include "measurements.h"
#include "widgets.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace GUI {

namespace {

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

} // namespace

/**
 * @brief   Creates the window over a session.
 * @param[in,out] session  The session whose settings the window edits; must outlive the window.
 * @note    Explicit bits beyond Core::MaxExplicitBits are not shown in the bit editor.
 */
ControlsWindow::ControlsWindow(GeneratorSession &session)
    : AppWindow("Signal Setup"), m_Session(session), m_BitInput(Core::MaxExplicitBits + 1, 0) {
    const auto &bits = m_Session.GetConfig().Bits;
    const auto count = std::min(bits.size(), m_BitInput.size() - 1);
    std::copy_n(bits.begin(), count, m_BitInput.begin());
}

void ControlsWindow::Draw() {
    const auto &config = m_Session.GetConfig();
    ImGui::PushItemWidth(std::clamp(ImGui::GetContentRegionAvail().x * .5f, 150.f, 360.f));
    DrawModulationSelector();
    const auto family = Core::WaveformFamily(config.Modulation);
    if (family == Core::Family::Noise) {
        DrawNoiseSourceControls();
    } else {
        DrawLinearControls(family == Core::Family::Fsk);
    }
    const std::string validation = ValidationError(config);
    if (!validation.empty()) {
        ImGui::TextColored(ErrorColor, "Invalid settings: %s", validation.c_str());
    }
    ImGui::PopItemWidth();
    DrawActions(validation.empty());
    DrawPresets();
}

void ControlsWindow::DrawModulationSelector() {
    auto &config = m_Session.GetConfig();
    if (ImGui::BeginCombo("Modulation", Core::ModulationName(config.Modulation))) {
        for (const auto &waveform : Core::Waveforms()) {
            const bool selected = waveform.Modulation == config.Modulation;
            if (ImGui::Selectable(waveform.CanonicalName.data(), selected)) {
                Core::SelectWaveform(config, waveform.Modulation);
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

void ControlsWindow::DrawNoiseSourceControls() {
    auto &config = m_Session.GetConfig();
    ImGui::InputInt("Sample count", &config.NoiseSource.SampleCount);
    Hint("Number of complex noise samples to generate.");
    ImGui::InputDouble("Sample rate (Hz)", &config.NoiseSource.SampleRateHz, 100, 1000, "%.6g");
    Hint("Sets the frequency axis of the spectrum: the PSD spans plus/minus half this rate.");
    ImGui::InputDouble("Noise power", &config.NoiseSource.NoisePower, .1, 1, "%.6g");
    Hint("Total complex noise power. Each of I and Q has half of it as variance.");
    ImGui::TextWrapped("Complex WGN: independent Gaussian I/Q components, each with variance power/2 before gain.");
    ImGui::InputScalar("Noise seed", ImGuiDataType_U32, &config.NoiseSeed);
    ImGui::InputDouble("Amplitude gain", &config.AmplitudeGain, .1, 1, "%.6g");
    Hint("Scales every sample once. Power scales with gain squared.");
}

/**
 * @brief   Draws the settings of symbol-based waveforms: timing, modulation-specific shaping, noise, impairments,
 *          seeds and data source.
 * @param[in] fsk  True for the frequency-shift family, which has tone settings instead of a pulse filter.
 */
void ControlsWindow::DrawLinearControls(bool fsk) {
    auto &config = m_Session.GetConfig();
    ImGui::InputInt("Symbol count", &config.SymbolCount);
    Hint("How many symbols to transmit. Each symbol carries log2(M) bits: 1 for BPSK, OOK, DBPSK, 2-FSK and MSK, 2 "
         "for QPSK, OQPSK, 4-PAM, 4-ASK, DQPSK, pi/4-DQPSK and 4-FSK, 3 for 8-PSK and 8-DPSK, 4 for 16-QAM, 5 for "
         "32-QAM, 6 for 64-QAM, 8 for 256-QAM.");
    ImGui::InputDouble("Symbol rate (Bd)", &config.SymbolRateBaud, 100, 1000, "%.6g");
    Hint("Symbols per second. Together with samples per symbol it sets the sample rate.");
    HelpButton(SymbolRateHelp);
    ImGui::InputInt("Samples per symbol", &config.SamplesPerSymbol);
    Hint("Oversampling factor (SPS). Higher values give smoother waveforms and more room in the spectrum, at the "
         "cost of more samples. RRC needs at least 2; FSK needs enough SPS to keep every tone below half the "
         "sample rate.");
    HelpButton(SPSHelp);
    ImGui::Text("Sample rate: %.6g Hz", config.SymbolRateBaud * config.SamplesPerSymbol);
    ImGui::InputDouble("Amplitude gain", &config.AmplitudeGain, .1, 1, "%.6g");
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

void ControlsWindow::DrawFrequencyModulationControls() {
    auto &config = m_Session.GetConfig();
    const bool locked = config.Modulation == Core::Modulation::MSK;
    ImGui::SeparatorText("Frequency Modulation");
    double spacing = Core::FskToneSpacingHz(config);
    ImGui::BeginDisabled(locked);
    if (ImGui::InputDouble("Tone spacing (Hz)", &spacing, 50, 500, "%.6g")) {
        config.ToneSpacingHz = spacing;
    }
    ImGui::EndDisabled();
    Hint("Frequency distance between adjacent tones. The modulation index is h = spacing / symbol rate; MSK "
         "fixes it at 0.5, the smallest spacing whose tones stay orthogonal over one symbol.");
    ImGui::Text("Modulation index h: %.4g%s", Core::FskModulationIndex(config), locked ? " (fixed for MSK)" : "");
    ImGui::TextWrapped(
        "Continuous phase, constant envelope. No pulse filter: output is exactly symbols x SPS samples.");
}

void ControlsWindow::DrawPulseShapingControls() {
    auto &config = m_Session.GetConfig();
    ImGui::SeparatorText("Pulse Shaping");
    int pulse = static_cast<int>(config.Pulse);
    if (ImGui::Combo("Pulse", &pulse, "Root-raised cosine\0Rectangular\0")) {
        config.Pulse = static_cast<Core::Pulse>(pulse);
    }
    Hint("RRC is the practical choice: with a matching receive filter it has no inter-symbol interference at "
         "the decision instants and a compact spectrum. Rectangular pulses open the eye fully but have wide "
         "sinc sidelobes.");
    HelpButton(PulseHelp);
    ImGui::BeginDisabled(config.Pulse != Core::Pulse::RRC);
    ImGui::InputDouble("RRC roll-off", &config.RollOff, .05, .1, "%.4g");
    Hint("Excess bandwidth of the root-raised-cosine filter, from 0 to 1. Occupied bandwidth is about symbol "
         "rate x (1 + roll-off). Low values give narrow spectra but longer, more sensitive filter tails.");
    HelpButton(RollOffHelp, {}, config.Pulse != Core::Pulse::RRC);
    ImGui::InputInt("RRC span (symbols)", &config.SpanSymbols);
    Hint("Filter length in symbols. Longer filters approximate the ideal response better (less residual ISI) "
         "and cost more samples.");
    HelpButton(SpanHelp, {}, config.Pulse != Core::Pulse::RRC);
    ImGui::EndDisabled();
}

void ControlsWindow::DrawAwgnControls() {
    auto &config = m_Session.GetConfig();
    ImGui::SeparatorText("AWGN");
    ImGui::Checkbox("Add AWGN", &config.Awgn.Enabled);
    Hint("Additive white Gaussian noise at the SNR below. Disabled keeps the clean signal.");
    ImGui::BeginDisabled(!config.Awgn.Enabled);
    ImGui::InputDouble("SNR (dB)", &config.Awgn.SnrDb, 1, 10, "%.6g");
    Hint("Clean signal power divided by added noise power, in dB. Lower values spread the constellation and close "
         "the eye.");
    {
        const auto ratios = ComputeEnergyRatios(config);
        HelpButton(SnrHelp,
                   FormatNumber("With your settings: SNR %.4g dB", config.Awgn.SnrDb) +
                       FormatNumber(" = Es/N0 %.4g dB", ratios.EsN0Db) +
                       FormatNumber(" = Eb/N0 %.4g dB", ratios.EbN0Db),
                   !config.Awgn.Enabled);
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("SNR is clean sample power over added complex noise power, measured on the steady-state "
                       "interval. Es/N0 = SNR + 10 log10(SPS); Eb/N0 = Es/N0 - 10 log10(bits per symbol).");
    if (config.Awgn.Enabled && std::isfinite(config.Awgn.SnrDb)) {
        const auto ratios = ComputeEnergyRatios(config);
        ImGui::Text("Equivalent: Es/N0 %.4g dB | Eb/N0 %.4g dB", ratios.EsN0Db, ratios.EbN0Db);
    }
}

void ControlsWindow::DrawImpairmentControls() {
    auto &config = m_Session.GetConfig();
    ImGui::SeparatorText("Channel impairments");
    auto &impairments = config.Impairments;
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
    ImGui::InputScalar("Impairment seed", ImGuiDataType_U32, &config.ImpairmentSeed);
    Hint("Seeds the phase-noise random walk; it is independent of the data and AWGN seeds.");
}

void ControlsWindow::DrawSeedControls() {
    auto &config = m_Session.GetConfig();
    ImGui::InputScalar("Noise seed", ImGuiDataType_U32, &config.NoiseSeed);
    ImGui::InputScalar("Random seed", ImGuiDataType_U32, &config.Seed);
    Hint("Same settings and seeds always reproduce the same bits and noise. The data and noise seeds are "
         "independent streams.");
}

/**
 * @brief   Draws the collapsible data-source settings, with the bit editor for explicit bits.
 */
void ControlsWindow::DrawAdvancedControls() {
    if (!ImGui::CollapsingHeader("Advanced")) {
        return;
    }
    auto &config = m_Session.GetConfig();
    int source = static_cast<int>(config.DataSource);
    if (ImGui::Combo("Data source", &source, "Seeded random bits\0Explicit bits\0")) {
        config.DataSource = static_cast<Core::DataSource>(source);
    }
    if (config.DataSource == Core::DataSource::Explicit) {
        if (ImGui::InputTextMultiline("Bits", m_BitInput.data(), m_BitInput.size(), ImVec2(-1, 70))) {
            config.Bits = m_BitInput.data();
        }
        ImGui::TextUnformatted("Enter exactly symbol count x bits per symbol digits (0 or 1).");
    }
}

/**
 * @brief   Copies the explicit bits of the settings into the bit editor, clearing what was there.
 */
void ControlsWindow::ResetBitInput() {
    const auto &bits = m_Session.GetConfig().Bits;
    std::fill(m_BitInput.begin(), m_BitInput.end(), 0);
    std::copy(bits.begin(), bits.end(), m_BitInput.begin());
}

/**
 * @brief   Draws the Generate and Export buttons, the export dialog, the progress note and the last error.
 * @param[in] settings_valid  False disables Generate.
 */
void ControlsWindow::DrawActions(bool settings_valid) {
    ImGui::Separator();
    ImGui::BeginDisabled(m_Session.IsGenerating() || !settings_valid);
    if (ImGui::Button("Generate Signal")) {
        m_Session.StartGeneration();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_Session.GetResult());
    if (ImGui::Button("Export I/Q...")) {
        m_ExportResult = m_Session.GetResult();
        m_ExportStatus.clear();
        m_ConfirmOverwrite = false;
        ImGui::OpenPopup("Export I/Q");
    }
    ImGui::EndDisabled();
    DrawExportDialog();
    if (m_Session.IsGenerating()) {
        ImGui::SameLine();
        ImGui::TextUnformatted("Generating...");
    }
    if (!m_Session.GetError().empty()) {
        ImGui::TextColored(ErrorColor, "Operation failed: %s", m_Session.GetError().c_str());
    }
}

/**
 * @brief   Draws the collapsible preset save/load controls and the notes of the last loaded preset.
 */
void ControlsWindow::DrawPresets() {
    if (ImGui::CollapsingHeader("Presets")) {
        ImGui::InputText("Preset path", m_PresetPath, sizeof m_PresetPath);
        Hint("Guided lessons ship in the presets/ folder, for example presets/01-qpsk-clean.preset. Loading one shows "
             "its notes below.");
        if (ImGui::Button("Save Preset")) {
            m_Session.SavePreset(m_PresetPath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Preset") && m_Session.LoadPreset(m_PresetPath)) {
            ResetBitInput();
        }
    }
    const auto &notes = m_Session.GetPresetNotes();
    if (!notes.empty()) {
        ImGui::SeparatorText("Preset notes");
        ImGui::TextWrapped("%s", notes.c_str());
    }
}

/**
 * @brief   Draws the I/Q export dialog once DrawActions() opens it.
 * @note    Exports the result captured when the dialog opened, even if a new generation finishes meanwhile, and
 *          asks before replacing an existing sample or metadata file.
 */
void ControlsWindow::DrawExportDialog() {
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
void ControlsWindow::Export(bool overwrite) {
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

} // namespace GUI
