/**
 * @file    plot_data.cpp
 * @brief   Decimated sample, constellation and frequency series that the GUI and image export plot.
 */

#include "plot_data.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Core {

namespace {

void AppendSample(PlotData &plot, const Core::GeneratedSignal &signal, const std::size_t index) {
    plot.Time.push_back(index / signal.SampleRateHz);
    plot.I.push_back(signal.Samples[index].real());
    plot.Q.push_back(signal.Samples[index].imag());
}

/**
 * @brief   Appends the minimum and maximum of I and Q in each bucket, so decimation keeps the peaks.
 * @param[in,out] plot      Receives the selected samples in time order.
 * @param[in]     signal    Signal with more than @p limit samples.
 * @param[in]     limit     Maximum number of points; the first and last sample are always kept.
 */
void AppendDecimatedSamples(PlotData &plot, const Core::GeneratedSignal &signal, const std::size_t limit) {
    const auto &samples = signal.Samples;
    AppendSample(plot, signal, 0);
    const auto bucket_count = (limit - 2) / 4;
    for (std::size_t bucket = 0; bucket < bucket_count; ++bucket) {
        const auto start = 1 + bucket * (samples.size() - 2) / bucket_count;
        const auto end = 1 + (bucket + 1) * (samples.size() - 2) / bucket_count;
        // Minimum I, maximum I, minimum Q, maximum Q
        std::array<std::size_t, 4> extremes{start, start, start, start};
        for (std::size_t k = start + 1; k < end; ++k) {
            if (samples[k].real() < samples[extremes[0]].real()) {
                extremes[0] = k;
            }
            if (samples[k].real() > samples[extremes[1]].real()) {
                extremes[1] = k;
            }
            if (samples[k].imag() < samples[extremes[2]].imag()) {
                extremes[2] = k;
            }
            if (samples[k].imag() > samples[extremes[3]].imag()) {
                extremes[3] = k;
            }
        }
        std::sort(extremes.begin(), extremes.end());
        const auto last = std::unique(extremes.begin(), extremes.end());
        for (auto it = extremes.begin(); it != last; ++it) {
            AppendSample(plot, signal, *it);
        }
    }
    AppendSample(plot, signal, samples.size() - 1);
}

void AppendFrequencyTrack(PlotData &plot, const Core::GeneratedSignal &signal, const std::size_t limit) {
    const auto estimate = Core::InstantaneousFrequency(signal.Samples, signal.SampleRateHz);
    const auto samples_per_symbol = static_cast<std::size_t>(signal.Config.SamplesPerSymbol);
    const auto stride = std::max<std::size_t>(1, estimate.size() / limit);
    for (std::size_t n = 0; n < estimate.size(); n += stride) {
        plot.FreqTime.push_back((static_cast<double>(n) + 0.5) / signal.SampleRateHz);
        plot.FreqEstimate.push_back(estimate[n]);
        plot.FreqNominal.push_back(
            signal.SymbolFrequenciesHz[std::min(n / samples_per_symbol, signal.SymbolFrequenciesHz.size() - 1)]);
    }
}

} // namespace

/**
 * @brief   Builds the plot series of a generated signal.
 * @param[in] signal    Generated signal; its sample rate must be positive and finite.
 * @param[in] limit     Maximum number of time-domain points (and frequency estimates); at least six. Longer
 *                      signals are decimated to the per-bucket extremes of I and Q.
 * @return  Time series, mapped symbols scaled by the amplitude gain, matched-filter observations (linear family
 *          only; empty for noise sources) and the frequency track (FSK family only).
 * @note    Throws std::invalid_argument when the sample rate or the limit is invalid.
 */
PlotData MakePlotData(const Core::GeneratedSignal &signal, const std::size_t limit) {
    if (!std::isfinite(signal.SampleRateHz) || signal.SampleRateHz <= 0) {
        throw std::invalid_argument("Plot sample rate must be positive and finite");
    }
    if (limit < 6) {
        throw std::invalid_argument("Plot limit must be at least six");
    }
    PlotData plot;
    if (signal.Samples.size() <= limit) {
        for (std::size_t k = 0; k < signal.Samples.size(); ++k) {
            AppendSample(plot, signal, k);
        }
    } else {
        AppendDecimatedSamples(plot, signal, limit);
    }
    for (const auto symbol : signal.Symbols) {
        plot.Mapped.X.push_back(symbol.real() * signal.Config.AmplitudeGain);
        plot.Mapped.Y.push_back(symbol.imag() * signal.Config.AmplitudeGain);
    }
    if (signal.Family == Core::Family::Fsk) {
        AppendFrequencyTrack(plot, signal, limit);
    }
    if (signal.Family == Core::Family::Linear) {
        for (const auto observation : Core::MatchedSymbols(signal).Values) {
            plot.Matched.X.push_back(observation.real());
            plot.Matched.Y.push_back(observation.imag());
        }
    }
    return plot;
}

} // namespace Core
