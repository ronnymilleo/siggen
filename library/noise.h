#pragma once
#include <complex>
#include <cstdint>
#include <random>
#include <vector>
#include "generator.h"
#include <span>

namespace iq {
// Deterministic standard-normal source: mt19937 with documented Box–Muller
// conversion. Uniforms are strictly inside (0,1): u = (raw + 0.5) / 2^32.
// Each pair of uniforms yields cos first, then sin; the sin value is cached
// and returned by the following call, so consecutive next() values alternate
// cos/sin from the same pair (fixed pairing order, I before Q).
class GaussianSource {
public:
    explicit GaussianSource(std::uint32_t seed) : engine_(seed) {}
    double next();
private:
    double uniform_open();
    std::mt19937 engine_;
    bool has_spare_ = false;
    double spare_ = 0;
};
// Complex white Gaussian noise: independent I/Q components, each with variance
// total_power / 2, scaled so the expected complex power equals total_power.
std::vector<std::complex<float>> gaussian_noise(std::size_t count, double total_power, std::uint32_t seed);
// Validate the conversion independently of the sample-dependent reference power.
double snr_power_ratio(double snr_db);
NoiseRecord add_awgn(std::span<std::complex<float>> samples, std::size_t begin,
                     std::size_t end, double snr_db, std::uint32_t seed);
}
