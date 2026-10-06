#pragma once
#include <complex>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace iq {
// Simple front-end/channel impairments applied to linear signals after AWGN.
// Every effect is off at its default value, so default configurations keep
// bit-identical output.
struct ImpairmentSettings {
    double cfo_hz = 0;                  // Carrier frequency offset; rotates the constellation.
    double phase_noise_linewidth_hz = 0; // Lorentzian 3 dB linewidth of a free-running oscillator.
    double iq_gain_db = 0;              // Q-branch gain relative to I.
    double iq_phase_deg = 0;            // Quadrature skew: Q leaks I at this angle.
    double dc_offset_i = 0;             // DC added to I, as a fraction of the signal RMS amplitude.
    double dc_offset_q = 0;             // DC added to Q, as a fraction of the signal RMS amplitude.
    int adc_bits = 0;                   // Quantizer resolution per component; 0 disables it.
    bool operator==(const ImpairmentSettings&) const = default;
    bool active() const { return *this != ImpairmentSettings{}; }
};
inline constexpr int MAX_ADC_BITS = 24;
// Throws std::invalid_argument for non-finite or out-of-range settings.
void validate(const ImpairmentSettings& settings);
// Applies, in order: CFO, phase noise, IQ imbalance, DC offset, quantization.
// Phase noise is a Wiener process with per-sample variance 2*pi*linewidth/fs,
// drawn from GaussianSource(seed). The quantizer is a mid-rise ADC whose full
// scale is the largest I or Q magnitude of the buffer (auto-ranging).
void apply_impairments(std::span<std::complex<float>> samples, double sample_rate_hz,
                       const ImpairmentSettings& settings, std::uint32_t seed);
// JSON object describing the settings and seed for sidecar metadata; `indent`
// prefixes every line after the first.
std::string impairments_json(const ImpairmentSettings& settings, std::uint32_t seed, const std::string& indent);
}
