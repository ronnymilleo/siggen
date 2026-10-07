/**
 * @file    impairments.cpp
 * @brief   Front-end and channel impairments applied to linear signals after AWGN.
 */

#include "impairments.h"

#include "noise.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace Core {

namespace {

constexpr double TwoPi = 2 * std::numbers::pi;

// CFO and phase noise together: a linear phase ramp plus a Wiener walk, as one rotation per sample
void ApplyPhaseRotation(std::span<std::complex<float>> samples, double sample_rate_hz,
                        const ImpairmentSettings &settings, std::uint32_t seed) {
    const double step = TwoPi * settings.CfoHz / sample_rate_hz;
    const double sigma = std::sqrt(TwoPi * settings.PhaseNoiseLinewidthHz / sample_rate_hz);
    GaussianSource source(seed);
    double walk = 0;
    for (std::size_t n = 0; n < samples.size(); ++n) {
        if (sigma > 0) {
            walk += sigma * source.Next();
        }
        const double theta = std::fmod(step * static_cast<double>(n), TwoPi) + walk;
        samples[n] *= std::complex<float>(static_cast<float>(std::cos(theta)), static_cast<float>(std::sin(theta)));
    }
}

void ApplyIqImbalance(std::span<std::complex<float>> samples, const ImpairmentSettings &settings) {
    const double gain = std::pow(10., settings.IqGainDb / 20);
    const double skew = settings.IqPhaseDeg * std::numbers::pi / 180;
    const auto cos_skew = static_cast<float>(gain * std::cos(skew));
    const auto sin_skew = static_cast<float>(gain * std::sin(skew));
    for (auto &sample : samples) {
        sample = {sample.real(), cos_skew * sample.imag() + sin_skew * sample.real()};
    }
}

void ApplyDcOffset(std::span<std::complex<float>> samples, const ImpairmentSettings &settings, double rms) {
    const std::complex<float> dc(static_cast<float>(settings.DcOffsetI * rms),
                                 static_cast<float>(settings.DcOffsetQ * rms));
    for (auto &sample : samples) {
        sample += dc;
    }
}

// Mid-rise quantizer whose full scale is the largest I or Q magnitude of the buffer
void Quantize(std::span<std::complex<float>> samples, int adc_bits) {
    float full_scale = 0;
    for (const auto &sample : samples) {
        full_scale = std::max({full_scale, std::abs(sample.real()), std::abs(sample.imag())});
    }
    if (full_scale <= 0) {
        return;
    }
    const float step = 2 * full_scale / static_cast<float>(1u << adc_bits);
    const float limit = full_scale - step / 2;
    const auto quantize = [&](float value) {
        return std::clamp((std::floor(value / step) + .5f) * step, -limit, limit);
    };
    for (auto &sample : samples) {
        sample = {quantize(sample.real()), quantize(sample.imag())};
    }
}

} // namespace

/**
 * @brief   Tells whether any impairment is enabled.
 * @return  True when any setting differs from its default.
 */
bool ImpairmentSettings::Active() const {
    return *this != ImpairmentSettings{};
}

/**
 * @brief   Checks impairment settings.
 * @param[in] settings  Settings to check.
 * @note    Throws std::invalid_argument for non-finite or out-of-range settings.
 */
void Validate(const ImpairmentSettings &settings) {
    const auto finite = [](double value) { return std::isfinite(value); };
    if (!finite(settings.CfoHz) || !finite(settings.PhaseNoiseLinewidthHz) || !finite(settings.IqGainDb) ||
        !finite(settings.IqPhaseDeg) || !finite(settings.DcOffsetI) || !finite(settings.DcOffsetQ)) {
        throw std::invalid_argument("Impairment settings must be finite");
    }
    if (settings.PhaseNoiseLinewidthHz < 0) {
        throw std::invalid_argument("Phase noise linewidth must be non-negative");
    }
    if (std::abs(settings.IqGainDb) > 40) {
        throw std::invalid_argument("IQ gain imbalance must be within +/-40 dB");
    }
    if (std::abs(settings.IqPhaseDeg) > 45) {
        throw std::invalid_argument("IQ phase imbalance must be within +/-45 degrees");
    }
    if (std::abs(settings.DcOffsetI) > 10 || std::abs(settings.DcOffsetQ) > 10) {
        throw std::invalid_argument("DC offset must be within +/-10 times the RMS amplitude");
    }
    if (settings.AdcBits < 0 || settings.AdcBits == 1 || settings.AdcBits > MaxAdcBits) {
        throw std::invalid_argument("ADC bits must be 0 (off) or 2-24");
    }
}

/**
 * @brief   Applies the enabled impairments to a buffer, in order: CFO, phase noise, IQ imbalance, DC offset,
 *          quantization.
 * @param[in,out] samples         Signal to degrade.
 * @param[in]     sample_rate_hz  Sample rate of the signal.
 * @param[in]     settings        Impairments to apply; validated first.
 * @param[in]     seed            Seed of the phase noise.
 * @note    Phase noise is a Wiener process with per-sample variance 2*pi*linewidth/fs, drawn from
 *          GaussianSource(seed). The quantizer is a mid-rise ADC whose full scale is the largest I or Q magnitude
 *          of the buffer (auto-ranging). DC offsets scale with the RMS amplitude of the input. Throws
 *          std::invalid_argument for invalid settings or sample rate and std::overflow_error when a sample
 *          overflows.
 */
void ApplyImpairments(std::span<std::complex<float>> samples, double sample_rate_hz, const ImpairmentSettings &settings,
                      std::uint32_t seed) {
    Validate(settings);
    if (!settings.Active() || samples.empty()) {
        return;
    }
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0) {
        throw std::invalid_argument("Sample rate must be positive and finite");
    }
    double power = 0;
    for (const auto &sample : samples) {
        power += std::norm(sample);
    }
    const double rms = std::sqrt(power / static_cast<double>(samples.size()));
    if (settings.CfoHz != 0 || settings.PhaseNoiseLinewidthHz > 0) {
        ApplyPhaseRotation(samples, sample_rate_hz, settings, seed);
    }
    if (settings.IqGainDb != 0 || settings.IqPhaseDeg != 0) {
        ApplyIqImbalance(samples, settings);
    }
    if (settings.DcOffsetI != 0 || settings.DcOffsetQ != 0) {
        ApplyDcOffset(samples, settings, rms);
    }
    if (settings.AdcBits > 0) {
        Quantize(samples, settings.AdcBits);
    }
    for (const auto &sample : samples) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::overflow_error("Impairment sample overflow");
        }
    }
}

/**
 * @brief   Describes impairment settings for sidecar metadata.
 * @param[in] settings  Settings to describe.
 * @param[in] seed      Phase noise seed.
 * @param[in] indent    Prefix of every line after the first.
 * @return  A JSON object with the settings, the seed and the order the effects are applied in.
 */
std::string ImpairmentsJson(const ImpairmentSettings &settings, std::uint32_t seed, const std::string &indent) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << "{\n"
        << indent << "  \"cfo_hz\": " << settings.CfoHz << ",\n"
        << indent << "  \"phase_noise_linewidth_hz\": " << settings.PhaseNoiseLinewidthHz << ",\n"
        << indent << "  \"iq_gain_db\": " << settings.IqGainDb << ",\n"
        << indent << "  \"iq_phase_deg\": " << settings.IqPhaseDeg << ",\n"
        << indent << "  \"dc_offset_i\": " << settings.DcOffsetI << ",\n"
        << indent << "  \"dc_offset_q\": " << settings.DcOffsetQ << ",\n"
        << indent << "  \"adc_bits\": " << settings.AdcBits << ",\n"
        << indent << "  \"seed\": " << seed << ",\n"
        << indent << "  \"order\": \"cfo, phase_noise, iq_imbalance, dc_offset, quantization\""
        << "\n"
        << indent << "}";
    return out.str();
}

} // namespace Core
