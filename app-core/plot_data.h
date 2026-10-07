#pragma once
#include "signal_analysis.h"
#include <algorithm>
#include <array>
#include <stdexcept>

struct XYData { std::vector<double> x, y; };
struct PlotData {
    std::vector<double> time, i, q;
    XYData mapped, matched;
    // FSK family: frequency estimate per sample pair against the nominal tone of each symbol.
    std::vector<double> freq_time, freq_estimate, freq_nominal;
};
inline PlotData make_plot_data(const iq::GeneratedSignal& signal, std::size_t limit = 4096) {
    if (!std::isfinite(signal.sample_rate_hz) || signal.sample_rate_hz <= 0)
        throw std::invalid_argument("Plot sample rate must be positive and finite");
    if (limit < 6) throw std::invalid_argument("Plot limit must be at least six");
    PlotData plot;
    const auto& samples = signal.samples;
    const auto append = [&](std::size_t k) {
        plot.time.push_back(k / signal.sample_rate_hz);
        plot.i.push_back(samples[k].real()); plot.q.push_back(samples[k].imag());
    };
    if (samples.size() <= limit) {
        for (std::size_t k = 0; k < samples.size(); ++k) append(k);
    } else {
        append(0);
        const auto buckets = (limit - 2) / 4;
        for (std::size_t b = 0; b < buckets; ++b) {
            const auto start = 1 + b * (samples.size() - 2) / buckets;
            const auto end = 1 + (b + 1) * (samples.size() - 2) / buckets;
            std::array<std::size_t, 4> index{start,start,start,start};
            for (std::size_t k = start + 1; k < end; ++k) {
                if (samples[k].real() < samples[index[0]].real()) index[0] = k;
                if (samples[k].real() > samples[index[1]].real()) index[1] = k;
                if (samples[k].imag() < samples[index[2]].imag()) index[2] = k;
                if (samples[k].imag() > samples[index[3]].imag()) index[3] = k;
            }
            std::sort(index.begin(), index.end());
            auto last = std::unique(index.begin(), index.end());
            for (auto it = index.begin(); it != last; ++it) append(*it);
        }
        append(samples.size()-1);
    }
    for (auto value : signal.symbols) {
        plot.mapped.x.push_back(value.real() * signal.config.amplitude_gain);
        plot.mapped.y.push_back(value.imag() * signal.config.amplitude_gain);
    }
    // Noise sources have no symbol constellation; leave matched observations empty.
    if (signal.family == iq::Family::Fsk) {
        const auto estimate = iq::instantaneous_frequency(samples, signal.sample_rate_hz);
        const auto sps = static_cast<std::size_t>(signal.config.samples_per_symbol);
        const auto stride = std::max<std::size_t>(1, estimate.size() / limit);
        for (std::size_t n = 0; n < estimate.size(); n += stride) {
            plot.freq_time.push_back((static_cast<double>(n) + 0.5) / signal.sample_rate_hz);
            plot.freq_estimate.push_back(estimate[n]);
            plot.freq_nominal.push_back(signal.symbol_frequencies_hz[std::min(n / sps, signal.symbol_frequencies_hz.size() - 1)]);
        }
    }
    if (signal.family == iq::Family::Linear)
        for (auto value : iq::matched_symbols(signal).values) {
            plot.matched.x.push_back(value.real()); plot.matched.y.push_back(value.imag());
        }
    return plot;
}
