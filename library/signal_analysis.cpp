#include "signal_analysis.h"
#include "signal_processing.h"
#include <stdexcept>
#include <cmath>
namespace iq {
SymbolObservations matched_symbols(const GeneratedSignal& r) {
    validate(r.config);
    if (r.family == Family::Noise || waveform_family(r.config.modulation) == Family::Noise)
        throw std::invalid_argument("Noise sources have no matched-symbol observations");
    SymbolObservations result;
    const auto sps = static_cast<std::size_t>(r.config.samples_per_symbol);
    const auto count = static_cast<std::size_t>(r.config.symbol_count);
    auto taps = r.config.pulse == Pulse::RRC ? RRCFilter(r.config.roll_off, r.config.span_symbols, r.config.samples_per_symbol)
                                          : std::vector<double>(sps, 1. / sps);
    const auto expected = (count - 1) * sps + taps.size();
    if (r.samples.size() != expected || r.symbols.size() != count ||
        r.filter_delay_samples != (r.config.pulse == Pulse::RRC ? (taps.size()-1)/2 : 0))
        throw std::invalid_argument("Signal dimensions or delay do not match configuration");
    for (auto sample : r.samples)
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag()))
            throw std::invalid_argument("Matched-filter input must be finite");
    // Exclude one complete RRC span on each edge for steady-state comparisons.
    const auto margin = r.config.pulse == Pulse::RRC ? static_cast<std::size_t>(r.config.span_symbols) : 0;
    for (std::size_t k = margin; k + margin < count; ++k) {
        const auto index = r.config.pulse == Pulse::RRC ? 2 * r.filter_delay_samples + k * sps : k * sps + sps - 1;
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
#include <numbers>
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
Spectrum welch_psd(const std::vector<std::complex<float>>& samples, double fs, std::size_t length) {
    if (!std::isfinite(fs) || fs <= 0 || !std::isfinite(1/fs) || length < 4 || length > 65536 || (length & (length-1)) || samples.size() > MAX_SIGNAL_SAMPLES)
        throw std::invalid_argument("PSD needs finite positive sample rate, bounded input and power-of-two segment length 4–65536");
    for (auto x : samples)
        if (!std::isfinite(x.real()) || !std::isfinite(x.imag())) throw std::invalid_argument("PSD input must be finite");
    Spectrum result;
    if (samples.size() < 4) return result;
    while (length > samples.size()) length >>= 1;
    result.segment_length = length;
    std::vector<double> window(length);
    double window_energy = 0;
    for (std::size_t k = 0; k < length; ++k) {
        window[k] = .5 - .5 * std::cos(2 * std::numbers::pi * k / length); // Periodic Hann.
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
