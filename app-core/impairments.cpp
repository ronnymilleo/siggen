#include "impairments.h"
#include "noise.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <numbers>
#include <stdexcept>

namespace iq {
void validate(const ImpairmentSettings& s) {
    const auto finite = [](double v) { return std::isfinite(v); };
    if (!finite(s.cfo_hz) || !finite(s.phase_noise_linewidth_hz) || !finite(s.iq_gain_db) ||
        !finite(s.iq_phase_deg) || !finite(s.dc_offset_i) || !finite(s.dc_offset_q))
        throw std::invalid_argument("Impairment settings must be finite");
    if (s.phase_noise_linewidth_hz < 0) throw std::invalid_argument("Phase noise linewidth must be non-negative");
    if (std::abs(s.iq_gain_db) > 40) throw std::invalid_argument("IQ gain imbalance must be within +/-40 dB");
    if (std::abs(s.iq_phase_deg) > 45) throw std::invalid_argument("IQ phase imbalance must be within +/-45 degrees");
    if (std::abs(s.dc_offset_i) > 10 || std::abs(s.dc_offset_q) > 10)
        throw std::invalid_argument("DC offset must be within +/-10 times the RMS amplitude");
    if (s.adc_bits < 0 || s.adc_bits == 1 || s.adc_bits > MAX_ADC_BITS)
        throw std::invalid_argument("ADC bits must be 0 (off) or 2-24");
}
void apply_impairments(std::span<std::complex<float>> samples, double sample_rate_hz,
                       const ImpairmentSettings& s, std::uint32_t seed) {
    validate(s);
    if (!s.active() || samples.empty()) return;
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0)
        throw std::invalid_argument("Sample rate must be positive and finite");
    constexpr double two_pi = 2 * std::numbers::pi;
    double power = 0;
    for (const auto& x : samples) power += std::norm(x);
    const double rms = std::sqrt(power / static_cast<double>(samples.size()));
    if (s.cfo_hz != 0 || s.phase_noise_linewidth_hz > 0) {
        const double step = two_pi * s.cfo_hz / sample_rate_hz;
        const double sigma = std::sqrt(two_pi * s.phase_noise_linewidth_hz / sample_rate_hz);
        GaussianSource source(seed);
        double walk = 0;
        for (std::size_t n = 0; n < samples.size(); ++n) {
            if (sigma > 0) walk += sigma * source.next();
            const double theta = std::fmod(step * static_cast<double>(n), two_pi) + walk;
            samples[n] *= std::complex<float>(static_cast<float>(std::cos(theta)), static_cast<float>(std::sin(theta)));
        }
    }
    if (s.iq_gain_db != 0 || s.iq_phase_deg != 0) {
        const double gain = std::pow(10., s.iq_gain_db / 20);
        const double skew = s.iq_phase_deg * std::numbers::pi / 180;
        const auto cos_skew = static_cast<float>(gain * std::cos(skew));
        const auto sin_skew = static_cast<float>(gain * std::sin(skew));
        for (auto& x : samples) x = {x.real(), cos_skew * x.imag() + sin_skew * x.real()};
    }
    if (s.dc_offset_i != 0 || s.dc_offset_q != 0) {
        const std::complex<float> dc(static_cast<float>(s.dc_offset_i * rms), static_cast<float>(s.dc_offset_q * rms));
        for (auto& x : samples) x += dc;
    }
    if (s.adc_bits > 0) {
        float full_scale = 0;
        for (const auto& x : samples) full_scale = std::max({full_scale, std::abs(x.real()), std::abs(x.imag())});
        if (full_scale > 0) {
            const float step = 2 * full_scale / static_cast<float>(1u << s.adc_bits);
            const float limit = full_scale - step / 2;
            const auto quantize = [&](float v) {
                return std::clamp((std::floor(v / step) + .5f) * step, -limit, limit);
            };
            for (auto& x : samples) x = {quantize(x.real()), quantize(x.imag())};
        }
    }
    for (const auto& x : samples)
        if (!std::isfinite(x.real()) || !std::isfinite(x.imag())) throw std::overflow_error("Impairment sample overflow");
}
std::string impairments_json(const ImpairmentSettings& s, std::uint32_t seed, const std::string& indent) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << "{\n" << indent << "  \"cfo_hz\": " << s.cfo_hz
        << ",\n" << indent << "  \"phase_noise_linewidth_hz\": " << s.phase_noise_linewidth_hz
        << ",\n" << indent << "  \"iq_gain_db\": " << s.iq_gain_db
        << ",\n" << indent << "  \"iq_phase_deg\": " << s.iq_phase_deg
        << ",\n" << indent << "  \"dc_offset_i\": " << s.dc_offset_i
        << ",\n" << indent << "  \"dc_offset_q\": " << s.dc_offset_q
        << ",\n" << indent << "  \"adc_bits\": " << s.adc_bits
        << ",\n" << indent << "  \"seed\": " << seed
        << ",\n" << indent << "  \"order\": \"cfo, phase_noise, iq_imbalance, dc_offset, quantization\""
        << "\n" << indent << "}";
    return out.str();
}
}
