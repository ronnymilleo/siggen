/**
 * @file    summary_window.cpp
 * @brief   The Signal Summary window: what was generated and how it measures.
 */

#include "summary_window.h"

#include "widgets.h"
#include <complex>
#include <string>

namespace GUI {

/**
 * @brief   Creates the window over a session.
 * @param[in] session  The session whose result the window shows; must outlive the window.
 */
SummaryWindow::SummaryWindow(const GeneratorSession &session) : AppWindow("Signal Summary"), m_Session(session) {
}

void SummaryWindow::Draw() {
    const auto &result = m_Session.GetResult();
    if (!result) {
        ImGui::TextDisabled("No signal generated yet. Choose settings and select Generate Signal.");
        return;
    }
    if (result->Config != m_Session.GetConfig()) {
        ImGui::TextColored(WarningColor, "Settings changed. Generate to update the displayed result.");
    }
    if (result->Family == Core::Family::Noise) {
        DrawNoiseSummary(*result);
    } else {
        DrawSymbolSummary(*result);
    }
    DrawMeasurements(*result);
}

void SummaryWindow::DrawNoiseSummary(const Core::GeneratedSignal &result) {
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

void SummaryWindow::DrawSymbolSummary(const Core::GeneratedSignal &result) {
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

void SummaryWindow::DrawMeasurements(const Core::GeneratedSignal &result) {
    const auto &power = m_Session.GetPowerStatistics();
    const auto &accuracy = m_Session.GetSymbolAccuracy();
    ImGui::SeparatorText("Measurements");
    Metric("Mean power", FormatNumber("%.4g", power.MeanPower), true);
    Hint("Mean of |x|^2 over every sample, filter transients included.");
    Metric("Peak power", FormatNumber("%.4g", power.PeakPower));
    Metric("PAPR", FormatNumber("%.3g dB", power.PaprDb));
    Hint("Peak-to-average power ratio. A constant-envelope signal has 0 dB; shaped QAM is several dB higher, which is "
         "what stresses power amplifiers.");
    if (accuracy) {
        Metric("EVM", FormatNumber("%.3g %%", 100 * accuracy->EvmRms), true);
        Hint("RMS error between the matched-filter observations and the ideal symbols, relative to the RMS ideal "
             "symbol. Even a clean RRC signal shows a small floor from the truncated filter.");
        Metric("EVM", FormatNumber("%.4g dB", accuracy->EvmDb));
        Metric("SNR after matched filter", FormatNumber("%.4g dB", accuracy->SnrAfterMatchedDb));
        Hint("-EVM in dB. Matched filtering averages noise over about SPS samples, so this exceeds the sample-level "
             "SNR by 10 log10(SPS).");
        if (result.Noise.AwgnApplied) {
            ImGui::TextWrapped("Requested sample SNR %.4g dB + 10 log10(SPS) = %.4g dB expected after the matched "
                               "filter. The measured value varies with the noise realization.",
                               result.Noise.RequestedSnrDb, result.Noise.RequestedSnrDb + accuracy->ExpectedOffsetDb);
        }
    }
}

} // namespace GUI
