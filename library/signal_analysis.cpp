#include "signal_analysis.h"
#include "signal_processing.h"
#include <stdexcept>
#include <cmath>
#include <numbers>
namespace iq {
std::vector<double> instantaneous_frequency(const std::vector<std::complex<float>>& samples, double sample_rate_hz) {
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0)
        throw std::invalid_argument("Sample rate must be positive and finite");
    std::vector<double> frequency;
    if (samples.size() < 2) return frequency;
    frequency.reserve(samples.size() - 1);
    for (std::size_t n = 0; n + 1 < samples.size(); ++n)
        frequency.push_back(std::arg(std::complex<double>(samples[n + 1]) * std::conj(std::complex<double>(samples[n]))) *
                            sample_rate_hz / (2 * std::numbers::pi));
    return frequency;
}
SymbolObservations matched_symbols(const GeneratedSignal& r) {
    validate(r.config);
    if (r.family != Family::Linear || waveform_family(r.config.modulation) != Family::Linear)
        throw std::invalid_argument("Only linear waveforms have matched-symbol observations");
    SymbolObservations result;
    const auto sps = static_cast<std::size_t>(r.config.samples_per_symbol);
    const auto count = static_cast<std::size_t>(r.config.symbol_count);
    auto taps = uses_rrc(r.config) ? RRCFilter(r.config.roll_off, r.config.span_symbols, r.config.samples_per_symbol)
                                          : std::vector<double>(sps, 1. / sps);
    const auto expected = (count - 1) * sps + taps.size();
    if (r.samples.size() != expected || r.symbols.size() != count ||
        r.filter_delay_samples != (uses_rrc(r.config) ? (taps.size()-1)/2 : 0))
        throw std::invalid_argument("Signal dimensions or delay do not match configuration");
    for (auto sample : r.samples)
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag()))
            throw std::invalid_argument("Matched-filter input must be finite");
    // Exclude one complete RRC span on each edge for steady-state comparisons.
    const auto margin = uses_rrc(r.config) ? static_cast<std::size_t>(r.config.span_symbols) : 0;
    for (std::size_t k = margin; k + margin < count; ++k) {
        const auto index = uses_rrc(r.config) ? 2 * r.filter_delay_samples + k * sps : k * sps + sps - 1;
        std::complex<double> value{};
        for (std::size_t j = 0; j < taps.size(); ++j) value += std::complex<double>(r.samples[index - j]) * taps[j];
        result.values.emplace_back(value);
        result.symbol_indices.push_back(k);
    }
    return result;
}
}

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <utility>
#include <vector>
namespace iq {
namespace {
void fft(std::vector<std::complex<double>>& data) {
    const auto n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i],data[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const auto root = std::polar(1., -2 * std::numbers::pi / length);
        for (std::size_t start = 0; start < n; start += length) {
            std::complex<double> phase{1,0};
            for (std::size_t j = 0; j < length/2; ++j) {
                const auto even = data[start+j];
                const auto odd = data[start+j+length/2] * phase;
                data[start+j] = even + odd;
                data[start+j+length/2] = even - odd;
                phase *= root;
            }
        }
    }
}
}
const char* window_name(Window window) {
    switch (window) {
    case Window::Hann: return "Hann";
    case Window::Hamming: return "Hamming";
    case Window::Blackman: return "Blackman";
    case Window::Rectangular: return "Rectangular";
    }
    return "Unknown";
}
double window_value(Window window, std::size_t k, std::size_t length) {
    const auto phase = 2 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(length);
    switch (window) {
    case Window::Hann: return .5 - .5 * std::cos(phase);
    case Window::Hamming: return .54 - .46 * std::cos(phase);
    case Window::Blackman: return .42 - .5 * std::cos(phase) + .08 * std::cos(2 * phase);
    case Window::Rectangular: return 1;
    }
    throw std::invalid_argument("Unknown window");
}
Spectrum welch_psd(const std::vector<std::complex<float>>& samples, double fs, std::size_t length, Window win) {
    if (!std::isfinite(fs) || fs <= 0 || !std::isfinite(1/fs) || length < 4 || length > 65536 || (length & (length-1)) || samples.size() > MAX_SIGNAL_SAMPLES)
        throw std::invalid_argument("PSD needs finite positive sample rate, bounded input and power-of-two segment length 4–65536");
    for (auto x : samples)
        if (!std::isfinite(x.real()) || !std::isfinite(x.imag())) throw std::invalid_argument("PSD input must be finite");
    Spectrum result;
    if (samples.size() < 4) return result;
    while (length > samples.size()) length >>= 1;
    result.segment_length = length;
    result.window = win;
    std::vector<double> window(length);
    double window_energy = 0;
    for (std::size_t k = 0; k < length; ++k) {
        window[k] = window_value(win, k, length);
        window_energy += window[k] * window[k];
    }
    result.power_density.assign(length,0);
    result.frequency_hz.resize(length);
    std::vector<std::complex<double>> frame(length);
    for (std::size_t start = 0; start <= samples.size()-length; start += length/2) {
        for (std::size_t k = 0; k < length; ++k) frame[k] = std::complex<double>(samples[start+k]) * window[k];
        fft(frame);
        for (std::size_t k = 0; k < length; ++k) result.power_density[k] += std::norm(frame[(k+length/2)%length]);
        ++result.segment_count;
    }
    for (std::size_t k = 0; k < length; ++k) {
        result.frequency_hz[k] = (static_cast<double>(k) - length/2) * (fs / length);
        result.power_density[k] /= window_energy;
        result.power_density[k] /= result.segment_count;
        result.power_density[k] /= fs;
        if (!std::isfinite(result.power_density[k])) throw std::overflow_error("PSD density overflow");
    }
    return result;
}
}
