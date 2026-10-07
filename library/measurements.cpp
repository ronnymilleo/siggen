#include "measurements.h"
#include "signal_analysis.h"
#include "signal_processing.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace iq {
namespace {
std::vector<double> matched_taps(const GenerationConfig& c) {
    const auto sps = static_cast<std::size_t>(c.samples_per_symbol);
    return uses_rrc(c) ? RRCFilter(c.roll_off, c.span_symbols, c.samples_per_symbol)
                                 : std::vector<double>(sps, 1. / static_cast<double>(sps));
}
}
PowerStatistics power_statistics(const std::vector<std::complex<float>>& samples) {
    PowerStatistics stats;
    if (samples.empty()) return stats;
    double total = 0;
    for (auto x : samples) {
        if (!std::isfinite(x.real()) || !std::isfinite(x.imag())) throw std::invalid_argument("Power statistics need finite samples");
        const double power = std::norm(std::complex<double>(x));
        total += power;
        stats.peak_power = std::max(stats.peak_power, power);
    }
    stats.mean_power = total / static_cast<double>(samples.size());
    if (stats.mean_power > 0) stats.papr_db = 10 * std::log10(stats.peak_power / stats.mean_power);
    return stats;
}
std::optional<SymbolAccuracy> symbol_accuracy(const GeneratedSignal& signal) {
    return symbol_accuracy(signal, 0, std::numeric_limits<std::size_t>::max());
}
std::optional<SymbolAccuracy> symbol_accuracy(const GeneratedSignal& signal, std::size_t first_symbol, std::size_t end_symbol) {
    if (signal.family != Family::Linear) return std::nullopt;
    const auto observations = matched_symbols(signal);
    double error = 0, reference = 0;
    std::size_t used = 0;
    for (std::size_t i = 0; i < observations.values.size(); ++i) {
        if (observations.symbol_indices[i] < first_symbol || observations.symbol_indices[i] >= end_symbol) continue;
        ++used;
        const auto ideal = std::complex<double>(signal.symbols[observations.symbol_indices[i]]) * signal.config.amplitude_gain;
        error += std::norm(std::complex<double>(observations.values[i]) - ideal);
        reference += std::norm(ideal);
    }
    if (used == 0 || reference <= 0) return std::nullopt;
    SymbolAccuracy accuracy;
    accuracy.symbol_count = used;
    accuracy.evm_rms = std::sqrt(error / reference);
    // Floor keeps the dB values finite for a noiseless signal.
    accuracy.evm_db = 20 * std::log10(std::max(accuracy.evm_rms, 1e-12));
    accuracy.snr_after_matched_db = -accuracy.evm_db;
    accuracy.expected_offset_db = 10 * std::log10(static_cast<double>(signal.config.samples_per_symbol));
    return accuracy;
}
EyeDiagram eye_diagram(const GeneratedSignal& r, std::size_t max_traces) {
    if (r.family != Family::Linear) throw std::invalid_argument("Only linear waveforms have a matched-filter eye diagram");
    if (max_traces == 0) throw std::invalid_argument("Eye diagram needs at least one trace");
    validate(r.config);
    const auto sps = static_cast<std::size_t>(r.config.samples_per_symbol);
    const auto count = static_cast<std::size_t>(r.config.symbol_count);
    const auto taps = matched_taps(r.config);
    const bool rrc = uses_rrc(r.config);
    const auto q_delay = quadrature_delay_samples(r.config);
    if (r.samples.size() != (count - 1) * sps + taps.size() + q_delay)
        throw std::invalid_argument("Signal dimensions do not match configuration");
    EyeDiagram eye;
    for (std::size_t k = 0; k <= 2 * sps; ++k)
        eye.time_symbols.push_back(static_cast<double>(k) / static_cast<double>(sps) - 1.);
    // Same steady-state symbols and decision instants as matched_symbols().
    const auto margin = rrc ? static_cast<std::size_t>(r.config.span_symbols) : 1;
    if (count <= 2 * margin) return eye;
    const auto available = count - 2 * margin;
    const auto used = std::min(available, max_traces);
    const auto output_length = r.samples.size() + taps.size() - 1;
    const auto filtered = [&](std::size_t n) {
        std::complex<double> value{};
        const auto first = n >= r.samples.size() ? n - r.samples.size() + 1 : 0;
        for (std::size_t j = first; j < taps.size() && j <= n; ++j)
            value += std::complex<double>(r.samples[n - j]) * taps[j];
        return value;
    };
    for (std::size_t t = 0; t < used; ++t) {
        const auto k = margin + t * available / used;
        const auto center = rrc ? 2 * r.filter_delay_samples + k * sps : k * sps + sps - 1;
        if (center < sps || center + q_delay + sps >= output_length) continue;
        std::vector<double> in_phase, quadrature;
        for (std::size_t n = center - sps; n <= center + sps; ++n) {
            in_phase.push_back(filtered(n).real());
            quadrature.push_back(filtered(n + q_delay).imag()); // OQPSK: Q is centred half a symbol later.
        }
        eye.in_phase.push_back(std::move(in_phase));
        eye.quadrature.push_back(std::move(quadrature));
    }
    return eye;
}
EnergyRatios snr_to_energy_ratios(double snr_db, int samples_per_symbol, int bits_per_symbol) {
    if (samples_per_symbol < 1 || bits_per_symbol < 1) throw std::invalid_argument("SPS and bits per symbol must be positive");
    const auto es_n0 = snr_db + 10 * std::log10(static_cast<double>(samples_per_symbol));
    return {es_n0, es_n0 - 10 * std::log10(static_cast<double>(bits_per_symbol))};
}
}
