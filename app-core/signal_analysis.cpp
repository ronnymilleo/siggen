/**
 * @file    signal_analysis.cpp
 * @brief   Receiver-side analysis: matched-filter symbol observations, instantaneous frequency and Welch PSD.
 */

#include "signal_analysis.h"

#include "signal_processing.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace Core {

namespace {

// Matched-filter output whose decision instant is sample `index`; for OQPSK (nonzero quadrature delay) the
// quadrature part is taken half a symbol later
std::complex<double> MatchedObservation(const std::vector<std::complex<float>> &samples,
                                        const std::vector<double> &taps, std::size_t index,
                                        std::size_t quadrature_delay) {
    std::complex<double> value{};
    for (std::size_t j = 0; j < taps.size(); ++j) {
        value += std::complex<double>(samples[index - j]) * taps[j];
    }
    if (quadrature_delay != 0) {
        double quadrature = 0;
        for (std::size_t j = 0; j < taps.size(); ++j) {
            quadrature += samples[index + quadrature_delay - j].imag() * taps[j];
        }
        value = {value.real(), quadrature};
    }
    return value;
}

// In-place radix-2 FFT; the size must be a power of two
void Fft(std::vector<std::complex<double>> &data) {
    const auto size = data.size();
    for (std::size_t i = 1, j = 0; i < size; ++i) {
        auto bit = size >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
    for (std::size_t length = 2; length <= size; length <<= 1) {
        const auto root = std::polar(1., -2 * std::numbers::pi / length);
        for (std::size_t start = 0; start < size; start += length) {
            std::complex<double> phase{1, 0};
            for (std::size_t j = 0; j < length / 2; ++j) {
                const auto even = data[start + j];
                const auto odd = data[start + j + length / 2] * phase;
                data[start + j] = even + odd;
                data[start + j + length / 2] = even - odd;
                phase *= root;
            }
        }
    }
}

std::expected<void, std::string> ValidateWelchInput(const std::vector<std::complex<float>> &samples,
                                                    double sample_rate_hz, std::size_t segment_length, Window window) {
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0 || !std::isfinite(1 / sample_rate_hz) ||
        segment_length < 4 || segment_length > 65536 || (segment_length & (segment_length - 1)) ||
        samples.size() > MaxSignalSamples) {
        return std::unexpected(std::string(
            "PSD needs finite positive sample rate, bounded input and power-of-two segment length 4–65536"));
    }
    if (!IsValidWindow(window)) {
        return std::unexpected(std::string("Unknown window"));
    }
    for (auto sample : samples) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            return std::unexpected(std::string("PSD input must be finite"));
        }
    }
    return {};
}

} // namespace

/**
 * @brief   Observes the steady-state symbols of a linear signal through its matched filter.
 * @param[in] signal  A generated linear signal; its configuration is validated first.
 * @return  One observation per symbol, excluding one complete RRC span on each edge; an error for non-linear
 *          signals, for a signal whose dimensions or delay do not match its configuration and for non-finite
 *          samples.
 */
std::expected<SymbolObservations, std::string> MatchedSymbols(const GeneratedSignal &signal) {
    const auto &config = signal.Config;
    if (const auto validated = Validate(config); !validated.has_value()) {
        return std::unexpected(validated.error());
    }
    if (signal.Family != Family::Linear || WaveformFamily(config.Modulation) != Family::Linear) {
        return std::unexpected(std::string("Only linear waveforms have matched-symbol observations"));
    }
    SymbolObservations result;
    const auto sps = static_cast<std::size_t>(config.SamplesPerSymbol);
    const auto count = static_cast<std::size_t>(config.SymbolCount);
    const bool rrc = UsesRrc(config);
    std::vector<double> taps;
    if (rrc) {
        const auto designed = RRCFilter(config.RollOff, config.SpanSymbols, config.SamplesPerSymbol);
        if (!designed.has_value()) {
            return std::unexpected(designed.error());
        }
        taps = std::move(*designed);
    } else {
        taps.assign(sps, 1. / sps);
    }
    const auto quadrature_delay = QuadratureDelaySamples(config);
    const auto expected_samples = (count - 1) * sps + taps.size() + quadrature_delay;
    if (signal.Samples.size() != expected_samples || signal.Symbols.size() != count ||
        signal.FilterDelaySamples != (rrc ? (taps.size() - 1) / 2 : 0)) {
        return std::unexpected(std::string("Signal dimensions or delay do not match configuration"));
    }
    for (auto sample : signal.Samples) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            return std::unexpected(std::string("Matched-filter input must be finite"));
        }
    }
    // Exclude one complete RRC span on each edge for steady-state comparisons
    const auto margin = rrc ? static_cast<std::size_t>(config.SpanSymbols) : 0;
    for (std::size_t k = margin; k + margin < count; ++k) {
        const auto index = rrc ? 2 * signal.FilterDelaySamples + k * sps : k * sps + sps - 1;
        result.Values.emplace_back(MatchedObservation(signal.Samples, taps, index, quadrature_delay));
        result.SymbolIndices.push_back(k);
    }
    return result;
}

/**
 * @brief   Estimates the frequency between consecutive samples.
 * @param[in] samples         Complex samples.
 * @param[in] sample_rate_hz  Sample rate Fs, positive and finite.
 * @return  arg(x[n+1] * conj(x[n])) * Fs / (2 pi) in Hz, one value fewer than the input (empty below two
 *          samples).
 * @note    The principal phase difference is taken, so the estimate is valid for |f| < Fs/2 and unaffected by
 *          phase wrapping. An error for an invalid sample rate.
 */
std::expected<std::vector<double>, std::string> InstantaneousFrequency(const std::vector<std::complex<float>> &samples,
                                                                       double sample_rate_hz) {
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0) {
        return std::unexpected(std::string("Sample rate must be positive and finite"));
    }
    std::vector<double> frequency;
    if (samples.size() < 2) {
        return frequency;
    }
    frequency.reserve(samples.size() - 1);
    for (std::size_t n = 0; n + 1 < samples.size(); ++n) {
        frequency.push_back(
            std::arg(std::complex<double>(samples[n + 1]) * std::conj(std::complex<double>(samples[n]))) *
            sample_rate_hz / (2 * std::numbers::pi));
    }
    return frequency;
}

/**
 * @brief   Returns the display name of a window.
 * @param[in] window  Window to name.
 * @return  "Hann", "Hamming", "Blackman" or "Rectangular".
 */
const char *WindowName(Window window) {
    switch (window) {
    case Window::Hann:
        return "Hann";
    case Window::Hamming:
        return "Hamming";
    case Window::Blackman:
        return "Blackman";
    case Window::Rectangular:
        return "Rectangular";
    }
    return "Unknown";
}

/**
 * @brief   Checks whether a value names a supported Welch window.
 * @param[in] window  Value to check, possibly read from untrusted input.
 * @return  True for one of the four window shapes.
 */
bool IsValidWindow(Window window) {
    switch (window) {
    case Window::Hann:
    case Window::Hamming:
    case Window::Blackman:
    case Window::Rectangular:
        return true;
    }
    return false;
}

/**
 * @brief   Returns one window coefficient, using the periodic (DFT-even) definition.
 * @param[in] window  Window shape; must be valid (see IsValidWindow).
 * @param[in] k       Coefficient index in [0, length).
 * @param[in] length  Window length.
 * @return  w[k]; zero for an unsupported value, which validated paths never produce.
 */
double WindowValue(Window window, std::size_t k, std::size_t length) {
    const auto phase = 2 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(length);
    switch (window) {
    case Window::Hann:
        return .5 - .5 * std::cos(phase);
    case Window::Hamming:
        return .54 - .46 * std::cos(phase);
    case Window::Blackman:
        return .42 - .5 * std::cos(phase) + .08 * std::cos(2 * phase);
    case Window::Rectangular:
        return 1;
    }
    return 0;
}

/**
 * @brief   Estimates the power spectral density with Welch's method.
 * @param[in] samples         Complex samples, finite and at most MaxSignalSamples.
 * @param[in] sample_rate_hz  Sample rate, positive and finite.
 * @param[in] segment_length  Power of two in [4, 65536]; halved until it fits the input.
 * @param[in] window          Window applied to every segment.
 * @return  The two-sided density over segments with 50 % overlap, ordered from -Fs/2; empty for fewer than four
 *          samples. An error for invalid input or when a density overflows.
 */
std::expected<Spectrum, std::string> WelchPsd(const std::vector<std::complex<float>> &samples, double sample_rate_hz,
                                              std::size_t segment_length, Window window) {
    if (const auto validated = ValidateWelchInput(samples, sample_rate_hz, segment_length, window);
        !validated.has_value()) {
        return std::unexpected(validated.error());
    }
    Spectrum result;
    if (samples.size() < 4) {
        return result;
    }
    while (segment_length > samples.size()) {
        segment_length >>= 1;
    }
    result.SegmentLength = segment_length;
    result.Window = window;
    std::vector<double> coefficients(segment_length);
    double window_energy = 0;
    for (std::size_t k = 0; k < segment_length; ++k) {
        coefficients[k] = WindowValue(window, k, segment_length);
        window_energy += coefficients[k] * coefficients[k];
    }
    result.PowerDensity.assign(segment_length, 0);
    result.FrequencyHz.resize(segment_length);
    std::vector<std::complex<double>> frame(segment_length);
    for (std::size_t start = 0; start <= samples.size() - segment_length; start += segment_length / 2) {
        for (std::size_t k = 0; k < segment_length; ++k) {
            frame[k] = std::complex<double>(samples[start + k]) * coefficients[k];
        }
        Fft(frame);
        for (std::size_t k = 0; k < segment_length; ++k) {
            result.PowerDensity[k] += std::norm(frame[(k + segment_length / 2) % segment_length]);
        }
        ++result.SegmentCount;
    }
    for (std::size_t k = 0; k < segment_length; ++k) {
        result.FrequencyHz[k] = (static_cast<double>(k) - segment_length / 2) * (sample_rate_hz / segment_length);
        result.PowerDensity[k] /= window_energy;
        result.PowerDensity[k] /= result.SegmentCount;
        result.PowerDensity[k] /= sample_rate_hz;
        if (!std::isfinite(result.PowerDensity[k])) {
            return std::unexpected(std::string("PSD density overflow"));
        }
    }
    return result;
}

} // namespace Core
