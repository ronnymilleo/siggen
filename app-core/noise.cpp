/**
 * @file    noise.cpp
 * @brief   Deterministic Gaussian noise: the standard-normal source, complex WGN and the AWGN overlay.
 */

#include "noise.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace Core {

/**
 * @brief   Creates a source whose sequence depends only on the seed.
 * @param[in] seed  mt19937 seed.
 */
GaussianSource::GaussianSource(std::uint32_t seed) : m_Engine(seed) {
}

/**
 * @brief   Draws the next standard-normal value.
 * @return  A value with zero mean and unit variance; calls alternate the cos and sin halves of one Box–Muller pair.
 */
double GaussianSource::Next() {
    if (m_HasSpare) {
        m_HasSpare = false;
        return m_Spare;
    }
    const double first_uniform = UniformOpen();
    const double second_uniform = UniformOpen();
    const double radius = std::sqrt(-2 * std::log(first_uniform));
    const double theta = 2 * std::numbers::pi * second_uniform;
    m_Spare = radius * std::sin(theta);
    m_HasSpare = true;
    return radius * std::cos(theta);
}

// Strictly inside (0,1), so the logarithm in Next() stays finite
double GaussianSource::UniformOpen() {
    return (static_cast<double>(m_Engine()) + 0.5) / 4294967296.0;
}

/**
 * @brief   Generates complex white Gaussian noise.
 * @param[in] count        Number of samples, at most 4194304.
 * @param[in] total_power  Expected complex power; each of I and Q has variance total_power / 2.
 * @param[in] seed         Seed of the GaussianSource; I is drawn before Q for each sample.
 * @return  The noise samples, or an error for a negative or non-finite power, above the sample limit, or when a
 *          value does not fit in a float.
 */
std::expected<std::vector<std::complex<float>>, std::string> GaussianNoise(std::size_t count, double total_power,
                                                                           std::uint32_t seed) {
    if (!std::isfinite(total_power) || total_power < 0) {
        return std::unexpected(std::string("Noise power must be finite and non-negative"));
    }
    if (count > 4 * 1024 * 1024) {
        return std::unexpected(std::string("Noise generation exceeds sample limit"));
    }
    const auto sigma = std::sqrt(total_power / 2);
    GaussianSource source(seed);
    std::vector<std::complex<float>> samples;
    samples.reserve(count);
    for (std::size_t k = 0; k < count; ++k) {
        const double in_phase = source.Next() * sigma;
        const double quadrature = source.Next() * sigma;
        if (!std::isfinite(in_phase) || !std::isfinite(quadrature) ||
            std::abs(in_phase) > std::numeric_limits<float>::max() ||
            std::abs(quadrature) > std::numeric_limits<float>::max()) {
            return std::unexpected(std::string("Gaussian noise overflow"));
        }
        samples.emplace_back(static_cast<float>(in_phase), static_cast<float>(quadrature));
    }
    return samples;
}

/**
 * @brief   Converts an SNR in dB to a power ratio.
 * @param[in] snr_db  Signal-to-noise ratio in dB.
 * @return  10^(snr_db / 10), or an error when the SNR or the ratio is not finite or the ratio underflows to zero.
 * @note    Validates the conversion independently of the sample-dependent reference power.
 */
std::expected<double, std::string> SnrPowerRatio(double snr_db) {
    const auto ratio = std::pow(10., snr_db / 10);
    if (!std::isfinite(snr_db) || !std::isfinite(ratio) || ratio <= 0) {
        return std::unexpected(std::string("AWGN SNR out of range"));
    }
    return ratio;
}

/**
 * @brief   Adds complex white Gaussian noise at a given SNR to every sample.
 * @param[in,out] samples  Signal to degrade.
 * @param[in]     begin    First sample of the interval whose mean power is the signal reference.
 * @param[in]     end      One past the last sample of that interval.
 * @param[in]     snr_db   Reference power divided by the added complex noise power, in dB.
 * @param[in]     seed     Noise seed.
 * @return  The provenance of the added noise, or an error for an empty or out-of-bounds interval, a non-positive
 *          reference power, or a sample overflow.
 */
std::expected<NoiseRecord, std::string> AddAwgn(std::span<std::complex<float>> samples, std::size_t begin,
                                                std::size_t end, double snr_db, std::uint32_t seed) {
    const auto ratio = SnrPowerRatio(snr_db);
    if (!ratio.has_value()) {
        return std::unexpected(ratio.error());
    }
    if (begin >= end || end > samples.size()) {
        return std::unexpected(std::string("AWGN reference interval is empty or out of bounds"));
    }
    double total = 0;
    for (auto k = begin; k < end; ++k) {
        total += std::norm(samples[k]);
    }
    const auto reference = total / static_cast<double>(end - begin);
    if (!std::isfinite(reference) || reference <= 0) {
        return std::unexpected(std::string("AWGN reference power must be positive"));
    }
    const auto power = reference / *ratio;
    if (!std::isfinite(power) || power <= 0) {
        return std::unexpected(std::string("AWGN noise power out of range"));
    }
    const auto noise = GaussianNoise(samples.size(), power, seed);
    if (!noise.has_value()) {
        return std::unexpected(noise.error());
    }
    for (std::size_t k = 0; k < samples.size(); ++k) {
        samples[k] += (*noise)[k];
        if (!std::isfinite(samples[k].real()) || !std::isfinite(samples[k].imag())) {
            return std::unexpected(std::string("AWGN sample overflow"));
        }
    }
    return NoiseRecord{true, snr_db, reference, begin, end, power, seed};
}

} // namespace Core
