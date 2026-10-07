/**
 * @file    measurements.cpp
 * @brief   Signal quality measurements: power statistics, symbol accuracy, eye diagram and energy ratios.
 */

#include "measurements.h"

#include "signal_analysis.h"
#include "signal_processing.h"
#include <cmath>
#include <stdexcept>

namespace Core {

namespace {

std::vector<double> MatchedTaps(const GenerationConfig &config) {
    const auto sps = static_cast<std::size_t>(config.SamplesPerSymbol);
    return UsesRrc(config) ? RRCFilter(config.RollOff, config.SpanSymbols, config.SamplesPerSymbol)
                           : std::vector<double>(sps, 1. / static_cast<double>(sps));
}

// Sample n of the full convolution of the samples with the taps, zero outside the buffer
std::complex<double> MatchedFilterOutput(const std::vector<std::complex<float>> &samples,
                                         const std::vector<double> &taps, std::size_t n) {
    std::complex<double> value{};
    const auto first = n >= samples.size() ? n - samples.size() + 1 : 0;
    for (std::size_t j = first; j < taps.size() && j <= n; ++j) {
        value += std::complex<double>(samples[n - j]) * taps[j];
    }
    return value;
}

} // namespace

/**
 * @brief   Measures mean and peak power over a sample buffer.
 * @param[in] samples  Samples to measure, transients included.
 * @return  Mean power, peak power and PAPR; all zero for an empty buffer.
 * @note    Throws std::invalid_argument for a non-finite sample.
 */
PowerStatistics MeasurePowerStatistics(const std::vector<std::complex<float>> &samples) {
    PowerStatistics stats;
    if (samples.empty()) {
        return stats;
    }
    double total = 0;
    for (auto sample : samples) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument("Power statistics need finite samples");
        }
        const double power = std::norm(std::complex<double>(sample));
        total += power;
        stats.PeakPower = std::max(stats.PeakPower, power);
    }
    stats.MeanPower = total / static_cast<double>(samples.size());
    if (stats.MeanPower > 0) {
        stats.PaprDb = 10 * std::log10(stats.PeakPower / stats.MeanPower);
    }
    return stats;
}

/**
 * @brief   Measures the EVM of a linear signal against its mapped constellation.
 * @param[in] signal  A generated signal.
 * @return  The accuracy over the steady-state symbols, or empty for noise and FSK sources, for signals without
 *          steady-state symbols and when the ideal symbols have no energy.
 */
std::optional<SymbolAccuracy> MeasureSymbolAccuracy(const GeneratedSignal &signal) {
    if (signal.Family != Family::Linear) {
        return std::nullopt;
    }
    const auto observations = MatchedSymbols(signal);
    if (observations.Values.empty()) {
        return std::nullopt;
    }
    double error = 0;
    double reference = 0;
    for (std::size_t i = 0; i < observations.Values.size(); ++i) {
        const auto ideal =
            std::complex<double>(signal.Symbols[observations.SymbolIndices[i]]) * signal.Config.AmplitudeGain;
        error += std::norm(std::complex<double>(observations.Values[i]) - ideal);
        reference += std::norm(ideal);
    }
    if (reference <= 0) {
        return std::nullopt;
    }
    SymbolAccuracy accuracy;
    accuracy.SymbolCount = observations.Values.size();
    accuracy.EvmRms = std::sqrt(error / reference);
    // Floor keeps the dB values finite for a noiseless signal
    accuracy.EvmDb = 20 * std::log10(std::max(accuracy.EvmRms, 1e-12));
    accuracy.SnrAfterMatchedDb = -accuracy.EvmDb;
    accuracy.ExpectedOffsetDb = 10 * std::log10(static_cast<double>(signal.Config.SamplesPerSymbol));
    return accuracy;
}

/**
 * @brief   Builds the matched-filter eye diagram of a linear signal.
 * @param[in] signal      A generated linear signal.
 * @param[in] max_traces  Upper bound on the traces, which bounds the cost; traces are taken uniformly across the
 *                        steady-state symbols.
 * @return  The traces, empty when there are no steady-state symbols.
 * @note    Uses the same steady-state symbols and decision instants as MatchedSymbols(). Throws
 *          std::invalid_argument for noise and FSK sources, for zero max_traces and when the signal does not
 *          match its configuration.
 */
EyeDiagram BuildEyeDiagram(const GeneratedSignal &signal, std::size_t max_traces) {
    if (signal.Family != Family::Linear) {
        throw std::invalid_argument("Only linear waveforms have a matched-filter eye diagram");
    }
    if (max_traces == 0) {
        throw std::invalid_argument("Eye diagram needs at least one trace");
    }
    const auto &config = signal.Config;
    Validate(config);
    const auto sps = static_cast<std::size_t>(config.SamplesPerSymbol);
    const auto count = static_cast<std::size_t>(config.SymbolCount);
    const auto taps = MatchedTaps(config);
    const bool rrc = UsesRrc(config);
    const auto quadrature_delay = QuadratureDelaySamples(config);
    if (signal.Samples.size() != (count - 1) * sps + taps.size() + quadrature_delay) {
        throw std::invalid_argument("Signal dimensions do not match configuration");
    }
    EyeDiagram eye;
    for (std::size_t k = 0; k <= 2 * sps; ++k) {
        eye.TimeSymbols.push_back(static_cast<double>(k) / static_cast<double>(sps) - 1.);
    }
    const auto margin = rrc ? static_cast<std::size_t>(config.SpanSymbols) : 1;
    if (count <= 2 * margin) {
        return eye;
    }
    const auto available = count - 2 * margin;
    const auto used = std::min(available, max_traces);
    const auto output_length = signal.Samples.size() + taps.size() - 1;
    for (std::size_t trace = 0; trace < used; ++trace) {
        const auto symbol = margin + trace * available / used;
        const auto center = rrc ? 2 * signal.FilterDelaySamples + symbol * sps : symbol * sps + sps - 1;
        if (center < sps || center + quadrature_delay + sps >= output_length) {
            continue;
        }
        std::vector<double> in_phase;
        std::vector<double> quadrature;
        for (std::size_t n = center - sps; n <= center + sps; ++n) {
            in_phase.push_back(MatchedFilterOutput(signal.Samples, taps, n).real());
            // OQPSK: Q is centred half a symbol later
            quadrature.push_back(MatchedFilterOutput(signal.Samples, taps, n + quadrature_delay).imag());
        }
        eye.InPhase.push_back(std::move(in_phase));
        eye.Quadrature.push_back(std::move(quadrature));
    }
    return eye;
}

/**
 * @brief   Converts a per-sample SNR to Es/N0 and Eb/N0.
 * @param[in] snr_db              Per-sample SNR in dB.
 * @param[in] samples_per_symbol  Samples per symbol, at least 1.
 * @param[in] bits_per_symbol     Bits per symbol, at least 1.
 * @return  Es/N0 = SNR + 10 log10(SPS) and Eb/N0 = Es/N0 - 10 log10(bits per symbol), in dB.
 * @note    Throws std::invalid_argument when either count is below 1.
 */
EnergyRatios SnrToEnergyRatios(double snr_db, int samples_per_symbol, int bits_per_symbol) {
    if (samples_per_symbol < 1 || bits_per_symbol < 1) {
        throw std::invalid_argument("SPS and bits per symbol must be positive");
    }
    const auto es_n0 = snr_db + 10 * std::log10(static_cast<double>(samples_per_symbol));
    return {es_n0, es_n0 - 10 * std::log10(static_cast<double>(bits_per_symbol))};
}

} // namespace Core
