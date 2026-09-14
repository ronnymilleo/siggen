#include "noise.h"
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <limits>

namespace iq {
double snr_power_ratio(double snr_db) {
    const auto ratio = std::pow(10., snr_db / 10);
    if (!std::isfinite(snr_db) || !std::isfinite(ratio) || ratio <= 0)
        throw std::invalid_argument("AWGN SNR out of range");
    return ratio;
}
NoiseRecord add_awgn(std::span<std::complex<float>> samples, std::size_t begin,
                     std::size_t end, double snr_db, std::uint32_t seed) {
    const auto ratio = snr_power_ratio(snr_db);
    if (begin >= end || end > samples.size())
        throw std::invalid_argument("AWGN reference interval is empty or out of bounds");
    double total = 0;
    for (auto k = begin; k < end; ++k) total += std::norm(samples[k]);
    const auto reference = total / static_cast<double>(end - begin);
    if (!std::isfinite(reference) || reference <= 0)
        throw std::invalid_argument("AWGN reference power must be positive");
    const auto power = reference / ratio;
    if (!std::isfinite(power) || power <= 0)
        throw std::invalid_argument("AWGN noise power out of range");
    const auto noise = gaussian_noise(samples.size(), power, seed);
    for (std::size_t k = 0; k < samples.size(); ++k) {
        samples[k] += noise[k];
        if (!std::isfinite(samples[k].real()) || !std::isfinite(samples[k].imag()))
            throw std::overflow_error("AWGN sample overflow");
    }
    return NoiseRecord{true, snr_db, reference, begin, end, power, seed};
}
double GaussianSource::uniform_open() {
    return (static_cast<double>(engine_()) + 0.5) / 4294967296.0;
}
double GaussianSource::next() {
    if (has_spare_) {
        has_spare_ = false;
        return spare_;
    }
    const double u1 = uniform_open();
    const double u2 = uniform_open();
    const double radius = std::sqrt(-2 * std::log(u1));
    const double theta = 2 * std::numbers::pi * u2;
    spare_ = radius * std::sin(theta);
    has_spare_ = true;
    return radius * std::cos(theta);
}
std::vector<std::complex<float>> gaussian_noise(std::size_t count, double total_power, std::uint32_t seed) {
    if (!std::isfinite(total_power) || total_power < 0)
        throw std::invalid_argument("Noise power must be finite and non-negative");
    if (count > 4 * 1024 * 1024) throw std::length_error("Noise generation exceeds sample limit");
    const auto sigma = std::sqrt(total_power / 2);
    GaussianSource source(seed);
    std::vector<std::complex<float>> samples;
    samples.reserve(count);
    for (std::size_t k = 0; k < count; ++k) {
        const double in_phase = source.next() * sigma;
        const double quadrature = source.next() * sigma;
        if (!std::isfinite(in_phase) || !std::isfinite(quadrature) ||
            std::abs(in_phase) > std::numeric_limits<float>::max() ||
            std::abs(quadrature) > std::numeric_limits<float>::max())
            throw std::overflow_error("Gaussian noise overflow");
        samples.emplace_back(static_cast<float>(in_phase), static_cast<float>(quadrature));
    }
    return samples;
}
}
