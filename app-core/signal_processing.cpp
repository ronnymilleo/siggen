/**
 * @file    signal_processing.cpp
 * @brief   Pulse-shaping filter design and the sample limit shared by every signal buffer.
 */

#include "signal_processing.h"

#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace Core {

namespace {

constexpr double Pi = std::numbers::pi;

double Sinc(double x) {
    return x == 0 ? 1 : std::sin(x) / x;
}

// Unnormalized RRC impulse response at `t` symbol periods from the centre (t >= 0)
double RRCImpulse(double beta, double t) {
    const double x = 4 * beta * t;
    if (t == 0) {
        return 1 + beta * (4 / Pi - 1);
    }
    if (beta == 0) {
        return Sinc(Pi * t);
    }
    if (std::abs(x - 1) < 0.25) {
        // Factor the removable zero analytically; no subtraction of nearly equal terms
        const double distance = x - 1;
        const double angle = Pi * t * (1 - beta);
        const double sinc = Sinc(Pi * distance / 4);
        return (std::sin(angle) * (-1 + x * Pi * Pi * distance / 8 * sinc * sinc) -
                x * std::cos(angle) * Pi / 2 * Sinc(Pi * distance / 2)) /
               (-Pi * t * (x + 1));
    }
    return (std::sin(Pi * t * (1 - beta)) + x * std::cos(Pi * t * (1 + beta))) / (Pi * t * (1 - x * x));
}

} // namespace

/**
 * @brief   Designs a root-raised-cosine pulse-shaping filter.
 * @param[in] beta  Roll-off factor in [0,1].
 * @param[in] span  Filter length in symbols, at least 1.
 * @param[in] sps   Samples per symbol, at least 2; span * sps must be even.
 * @return  span * sps + 1 taps with unit energy, symmetric around the centre tap.
 * @note    Throws std::invalid_argument for out-of-range settings or when the tap count would exceed
 *          MaxSignalSamples.
 */
std::vector<double> RRCFilter(double beta, int span, int sps) {
    if (!std::isfinite(beta) || beta < 0 || beta > 1 || span < 1 || sps < 2 ||
        static_cast<std::size_t>(span) > (MaxSignalSamples - 1) / static_cast<std::size_t>(sps)) {
        throw std::invalid_argument("RRC requires beta in [0,1], positive span, SPS >= 2 and bounded tap count");
    }
    const auto order = static_cast<std::size_t>(span) * static_cast<std::size_t>(sps);
    if (order % 2 != 0) {
        throw std::invalid_argument("RRC filter order (span * SPS) must be even");
    }
    std::vector<double> taps(order + 1);
    double energy = 0;
    for (std::size_t i = 0; i <= order; ++i) {
        const double t = std::abs((static_cast<double>(i) - static_cast<double>(order) / 2) / sps);
        const double tap = RRCImpulse(beta, t);
        taps[i] = tap;
        energy += tap * tap;
    }
    for (auto &tap : taps) {
        tap /= std::sqrt(energy);
    }
    return taps;
}

} // namespace Core
