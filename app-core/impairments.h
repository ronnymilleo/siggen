/**
 * @file    impairments.h
 * @brief   Front-end and channel impairments applied to linear signals after AWGN.
 */

#ifndef SIGGEN_IMPAIRMENTS_H
#define SIGGEN_IMPAIRMENTS_H

#include <complex>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Core {

inline constexpr int MaxAdcBits = 24;

/**
 * @struct  ImpairmentSettings
 * @brief   Simple front-end and channel impairments applied to linear signals after AWGN.
 * @details Every effect is off at its default value, so default configurations keep bit-identical output.
 */
struct ImpairmentSettings {
    double CfoHz = 0;                 // Carrier frequency offset; rotates the constellation
    double PhaseNoiseLinewidthHz = 0; // Lorentzian 3 dB linewidth of a free-running oscillator
    double IqGainDb = 0;              // Q-branch gain relative to I
    double IqPhaseDeg = 0;            // Quadrature skew: Q leaks I at this angle
    double DcOffsetI = 0;             // DC added to I, as a fraction of the signal RMS amplitude
    double DcOffsetQ = 0;             // DC added to Q, as a fraction of the signal RMS amplitude
    int AdcBits = 0;                  // Quantizer resolution per component; 0 disables it
    bool operator==(const ImpairmentSettings &) const = default;
    bool Active() const;
};

void Validate(const ImpairmentSettings &settings);
void ApplyImpairments(std::span<std::complex<float>> samples, double sample_rate_hz, const ImpairmentSettings &settings,
                      std::uint32_t seed);
std::string ImpairmentsJson(const ImpairmentSettings &settings, std::uint32_t seed, const std::string &indent);

} // namespace Core

#endif // SIGGEN_IMPAIRMENTS_H
