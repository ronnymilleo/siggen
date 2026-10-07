/**
 * @file    noise.h
 * @brief   Deterministic Gaussian noise: the standard-normal source, complex WGN and the AWGN overlay.
 */

#ifndef SIGGEN_NOISE_H
#define SIGGEN_NOISE_H

#include "generator.h"
#include <complex>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace Core {

/**
 * @class   GaussianSource
 * @brief   Deterministic standard-normal source: mt19937 with a documented Box–Muller conversion.
 * @details Uniforms are strictly inside (0,1): u = (raw + 0.5) / 2^32. Each pair of uniforms yields cos first,
 *          then sin; the sin value is cached and returned by the following call, so consecutive Next() values
 *          alternate cos/sin from the same pair (fixed pairing order, I before Q).
 */
class GaussianSource {
public:
    explicit GaussianSource(std::uint32_t seed);

    double Next();

private:
    double UniformOpen();

    std::mt19937 m_Engine;
    bool m_HasSpare = false;
    double m_Spare = 0;
};

std::vector<std::complex<float>> GaussianNoise(std::size_t count, double total_power, std::uint32_t seed);

double SnrPowerRatio(double snr_db);
NoiseRecord AddAwgn(std::span<std::complex<float>> samples, std::size_t begin, std::size_t end, double snr_db,
                    std::uint32_t seed);

} // namespace Core

#endif // SIGGEN_NOISE_H
