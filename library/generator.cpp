#include "generator.h"
#include "noise.h"
#include "signal_processing.h"
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace iq {
namespace {
// 8-PSK: phases k*pi/4 counterclockwise carry labels 000,001,011,010,110,111,101,100.
constexpr int psk8_phase_index[8] = {0, 1, 3, 2, 7, 6, 4, 5};
// 64-QAM axis levels by three-bit Gray label, ascending amplitude order, /sqrt(42).
constexpr int qam64_axis_level[8] = {-7, -5, -1, -3, 7, 5, 1, 3};
// Inverse of the binary-reflected Gray code: label -> ascending level index.
int gray_index(int label) {
    int index = label;
    for (int shift = 1; (label >> shift) > 0; ++shift) index ^= label >> shift;
    return index;
}
// Gray-labelled amplitude on an axis of 2^bits levels: odd integers -(2^bits-1)..+(2^bits-1).
int gray_level(int label, int bits) { return 2 * gray_index(label) - ((1 << bits) - 1); }
int bits_value(const std::string& bits, std::size_t offset, int count) {
    int value = 0;
    for (int k = 0; k < count; ++k) value = (value << 1) | (bits[offset + static_cast<std::size_t>(k)] == '1' ? 1 : 0);
    return value;
}
// Clean steady-state reference interval for AWGN, excluding RRC transients.
std::pair<std::size_t, std::size_t> awgn_reference_interval(const GenerationConfig& c, std::size_t sample_count) {
    const auto sps = static_cast<std::size_t>(c.samples_per_symbol);
    if (uses_rrc(c)) {
        const auto begin = static_cast<std::size_t>(c.span_symbols) * sps;
        const auto end = static_cast<std::size_t>(c.symbol_count) * sps;
        if (begin >= end || end > sample_count) throw std::invalid_argument("AWGN reference interval is empty");
        return {begin, end};
    }
    if (sample_count == 0) throw std::invalid_argument("AWGN reference interval is empty");
    return {0, sample_count};
}
void apply_awgn(GeneratedSignal& result) {
    const auto [begin, end] = awgn_reference_interval(result.config, result.samples.size());
    result.noise = add_awgn(result.samples, begin, end, result.config.awgn.snr_db,
                            result.config.noise_seed);
}
}
namespace {
// AWGN, then channel impairments, for every non-noise family.
void finish(GeneratedSignal& result) {
    const auto& config = result.config;
    if (config.awgn.enabled) apply_awgn(result);
    if (config.impairments.active()) {
        apply_impairments(result.samples, result.sample_rate_hz, config.impairments, config.impairment_seed);
        result.impairments_applied = true;
    }
}
// Continuous-phase FSK: phase[n+1] = phase[n] + 2 pi f[n] / Fs, starting at zero.
// Labels ascend in tone: 0,1 (2-FSK) and 00,01,11,10 (4-FSK).
void generate_fsk(GeneratedSignal& result, const std::string& bits) {
    const auto& c = result.config;
    const auto bps = static_cast<std::size_t>(bits_per_symbol(c.modulation));
    const auto sps = static_cast<std::size_t>(c.samples_per_symbol);
    const auto spacing = fsk_tone_spacing_hz(c);
    const auto levels = static_cast<double>(1 << bps);
    result.samples.resize(static_cast<std::size_t>(c.symbol_count) * sps);
    result.symbol_frequencies_hz.reserve(static_cast<std::size_t>(c.symbol_count));
    constexpr double two_pi = 2 * std::numbers::pi;
    double phase = 0;
    for (std::size_t k = 0; k < static_cast<std::size_t>(c.symbol_count); ++k) {
        const auto tone = gray_index(bits_value(bits, k * bps, static_cast<int>(bps)));
        const auto frequency = (static_cast<double>(tone) - (levels - 1) / 2) * spacing;
        result.symbol_frequencies_hz.push_back(frequency);
        const auto increment = two_pi * frequency / result.sample_rate_hz;
        for (std::size_t j = 0; j < sps; ++j) {
            result.samples[k * sps + j] = std::complex<float>(static_cast<float>(c.amplitude_gain * std::cos(phase)),
                                                              static_cast<float>(c.amplitude_gain * std::sin(phase)));
            phase = std::remainder(phase + increment, two_pi);
        }
    }
}
}
bool uses_rrc(const GenerationConfig& c) {
    return waveform_family(c.modulation) == Family::Linear && c.pulse == Pulse::RRC;
}
double fsk_tone_spacing_hz(const GenerationConfig& c) {
    return c.modulation == Modulation::MSK ? c.symbol_rate_baud * MSK_MODULATION_INDEX : c.tone_spacing_hz;
}
double fsk_modulation_index(const GenerationConfig& c) { return fsk_tone_spacing_hz(c) / c.symbol_rate_baud; }
void select_waveform(GenerationConfig& config, Modulation modulation) {
    if (waveform_family(config.modulation) != waveform_family(modulation)) {
        GenerationConfig defaults;
        defaults.amplitude_gain = config.amplitude_gain;
        defaults.seed = config.seed;
        if (waveform_family(modulation) == Family::Fsk) defaults.pulse = Pulse::Rectangular;
        defaults.noise_seed = config.noise_seed;
        defaults.impairment_seed = config.impairment_seed;
        config = defaults;
    }
    config.modulation = modulation;
}
void validate(const GenerationConfig& c) {
    if (!is_valid(c.modulation)) throw std::invalid_argument("Unsupported modulation");
    const auto& info = waveform_descriptor(c.modulation);
    if (!std::isfinite(c.amplitude_gain) || c.amplitude_gain < 0 || c.amplitude_gain > 1000000)
        throw std::invalid_argument("Amplitude gain must be finite and in [0,1000000]");
    if (c.data_source != DataSource::Random && c.data_source != DataSource::Explicit)
        throw std::invalid_argument("Unsupported data source");
    if (c.bits.size() > MAX_EXPLICIT_BITS) throw std::invalid_argument("Bit input exceeds limit");
    // Persisted fields are always validated so presets fail transactionally,
    // even when the active waveform family ignores them.
    const int bps = info.bits_per_symbol;
    if (c.symbol_count < 1 || c.symbol_count > 65536) throw std::invalid_argument("Symbol count must be 1–65536");
    if (c.samples_per_symbol < 1 || c.samples_per_symbol > 32) throw std::invalid_argument("Samples per symbol must be 1–32");
    if (!std::isfinite(c.symbol_rate_baud) || c.symbol_rate_baud <= 0 ||
        !std::isfinite(c.symbol_rate_baud * c.samples_per_symbol) ||
        !std::isfinite(MAX_SIGNAL_SAMPLES / (c.symbol_rate_baud * c.samples_per_symbol)))
        throw std::invalid_argument("Symbol rate must be positive with finite sample rate and duration");
    if (c.pulse != Pulse::RRC && c.pulse != Pulse::Rectangular) throw std::invalid_argument("Unsupported pulse");
    if (!std::isfinite(c.roll_off) || c.roll_off < 0 || c.roll_off > 1 || c.span_symbols < 1 || c.span_symbols > 100)
        throw std::invalid_argument("Roll-off must be [0,1] and span must be 1–100 symbols");
    if (info.family != Family::Fsk && c.pulse == Pulse::RRC && (c.samples_per_symbol < 2 || (c.span_symbols * c.samples_per_symbol) % 2))
        throw std::invalid_argument("RRC needs SPS >= 2 and even span * SPS");
    const auto sps = static_cast<std::size_t>(c.samples_per_symbol);
    const auto tail = uses_rrc(c) ? static_cast<std::size_t>(c.span_symbols) * sps + 1 : sps;
    if (static_cast<std::size_t>(c.symbol_count - 1) > (MAX_SIGNAL_SAMPLES - tail) / sps)
        throw std::length_error("Generation exceeds sample limit");
    if (c.noise_source.sample_count < 1 || static_cast<std::size_t>(c.noise_source.sample_count) > MAX_SIGNAL_SAMPLES)
        throw std::invalid_argument("Noise sample count must be 1–4194304");
    if (!std::isfinite(c.noise_source.sample_rate_hz) || c.noise_source.sample_rate_hz <= 0 ||
        !std::isfinite(MAX_SIGNAL_SAMPLES / c.noise_source.sample_rate_hz))
        throw std::invalid_argument("Noise sample rate must be positive with finite duration");
    if (!std::isfinite(c.noise_source.noise_power) || c.noise_source.noise_power < 0)
        throw std::invalid_argument("Noise power must be finite and non-negative");
    if (!std::isfinite(c.awgn.snr_db))
        throw std::invalid_argument("AWGN SNR must be finite");
    validate(c.impairments);
    if (!std::isfinite(c.tone_spacing_hz) || c.tone_spacing_hz <= 0)
        throw std::invalid_argument("Tone spacing must be positive and finite");
    if (info.family == Family::Fsk) {
        // Every tone centre must lie strictly inside Nyquist; the spectral skirts of
        // rectangular frequency transitions may still alias at low SPS.
        const auto outer = (static_cast<double>(1 << info.bits_per_symbol) - 1) / 2 * fsk_tone_spacing_hz(c);
        if (!(outer < c.symbol_rate_baud * c.samples_per_symbol / 2))
            throw std::invalid_argument("FSK tone centres must lie strictly below half the sample rate");
    }
    if (info.family == Family::Noise) {
        if (c.impairments.active()) throw std::invalid_argument("Impairments do not apply to noise sources");
        if (!info.awgn_supported && c.awgn.enabled) throw std::invalid_argument("AWGN does not apply to noise sources");
        if (!info.explicit_input && c.data_source == DataSource::Explicit)
            throw std::invalid_argument("Noise sources use seeded random generation");
        return;
    }
    if (c.data_source == DataSource::Explicit &&
        (c.bits.size() != static_cast<std::size_t>(c.symbol_count) * static_cast<std::size_t>(bps) ||
         c.bits.find_first_not_of("01") != std::string::npos))
        throw std::invalid_argument("Explicit input must contain exactly symbol count * bits per symbol binary digits");
}
std::vector<std::complex<float>> map_symbols(Modulation modulation, const std::string& bits) {
    const auto& info = waveform_descriptor(modulation);
    if (info.family != Family::Linear) throw std::invalid_argument("Only linear waveforms have a symbol mapping");
    const auto bps = static_cast<std::size_t>(info.bits_per_symbol);
    if (bits.size() > 65536 * bps || bits.size() % bps || bits.find_first_not_of("01") != std::string::npos)
        throw std::invalid_argument("Invalid mapper bit input");
    std::vector<std::complex<float>> symbols;
    symbols.reserve(bits.size() / bps);
    const float a = 1.f / std::sqrt(2.f);
    // Differential schemes carry state: the phase accumulates the data increments.
    int dbpsk_state = 0, dqpsk_state = 0;
    for (std::size_t i = 0; i < bits.size(); i += bps) {
        const float real = bits[i] == '0' ? 1.f : -1.f;
        if (modulation == Modulation::BPSK) symbols.emplace_back(real, 0);
        else if (modulation == Modulation::QAM16) {
            // Axis pairs 00 -> -3, 01 -> -1, 11 -> +1, 10 -> +3.
            const auto level = [&](std::size_t k) {
                return (bits[k] == '0' ? -1.f : 1.f) * (bits[k+1] == '0' ? 3.f : 1.f) / std::sqrt(10.f);
            };
            symbols.emplace_back(level(i), level(i+2));
        }
        else if (modulation == Modulation::PSK8) {
            const auto k = psk8_phase_index[bits_value(bits, i, 3)];
            const auto angle = static_cast<double>(k) * std::numbers::pi / 4;
            symbols.emplace_back(static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)));
        }
        else if (modulation == Modulation::QAM64) {
            const auto scale = 1.f / std::sqrt(42.f);
            symbols.emplace_back(qam64_axis_level[bits_value(bits, i, 3)] * scale,
                                 qam64_axis_level[bits_value(bits, i + 3, 3)] * scale);
        }
        else if (modulation == Modulation::QAM256) {
            const auto scale = 1.f / std::sqrt(170.f);
            symbols.emplace_back(static_cast<float>(gray_level(bits_value(bits, i, 4), 4)) * scale,
                                 static_cast<float>(gray_level(bits_value(bits, i + 4, 4), 4)) * scale);
        }
        else if (modulation == Modulation::PAM4)
            symbols.emplace_back(static_cast<float>(gray_level(bits_value(bits, i, 2), 2)) / std::sqrt(5.f), 0);
        else if (modulation == Modulation::OOK)
            symbols.emplace_back(bits[i] == '1' ? std::sqrt(2.f) : 0.f, 0); // Mean energy 1.
        else if (modulation == Modulation::DBPSK) {
            dbpsk_state ^= bits[i] == '1' ? 1 : 0;
            symbols.emplace_back(dbpsk_state ? -1.f : 1.f, 0);
        }
        else if (modulation == Modulation::DQPSK) {
            // Gray phase increments 00 -> 0, 01 -> +90, 11 -> +180, 10 -> +270 degrees.
            constexpr int increment[4] = {0, 1, 3, 2};
            dqpsk_state = (dqpsk_state + increment[bits_value(bits, i, 2)]) % 4;
            static constexpr float sign_i[4] = {1.f, -1.f, -1.f, 1.f}, sign_q[4] = {1.f, 1.f, -1.f, -1.f};
            symbols.emplace_back(sign_i[dqpsk_state] * a, sign_q[dqpsk_state] * a);
        }
        else if (modulation == Modulation::QPSK)
            symbols.emplace_back(real / std::sqrt(2.f), (bits[i+1] == '0' ? 1.f : -1.f) / std::sqrt(2.f));
        else throw std::invalid_argument("Waveform has no implemented symbol mapper");
    }
    return symbols;
}
GeneratedSignal generate(const GenerationConfig& config) {
    validate(config);
    GeneratedSignal result;
    result.config = config;
    result.family = waveform_family(config.modulation);
    if (result.family == Family::Noise) {
        result.sample_rate_hz = config.noise_source.sample_rate_hz;
        result.samples = gaussian_noise(static_cast<std::size_t>(config.noise_source.sample_count),
                                        config.noise_source.noise_power, config.noise_seed);
        if (config.amplitude_gain != 1)
            for (auto& sample : result.samples) sample *= static_cast<float>(config.amplitude_gain);
        result.noise = NoiseRecord{false, 0, 0, 0, 0, config.noise_source.noise_power, config.noise_seed};
        return result;
    }
    std::string bits = config.bits;
    if (config.data_source == DataSource::Random) {
        bits.resize(static_cast<std::size_t>(config.symbol_count) * static_cast<std::size_t>(bits_per_symbol(config.modulation)));
        std::mt19937 rng(config.seed);
        for (char& bit : bits) bit = (rng() & 1u) ? '1' : '0'; // One engine output per bit, least significant bit.
    }
    result.sample_rate_hz = config.symbol_rate_baud * config.samples_per_symbol;
    if (result.family == Family::Fsk) {
        generate_fsk(result, bits);
        finish(result);
        return result;
    }
    result.symbols = map_symbols(config.modulation, bits);
    const auto sps = static_cast<std::size_t>(config.samples_per_symbol);
    std::vector<double> taps;
    if (config.pulse == Pulse::RRC) {
        taps = RRCFilter(config.roll_off, config.span_symbols, config.samples_per_symbol);
        result.filter_delay_samples = (taps.size() - 1) / 2;
    } else taps.assign(sps, 1.);
    result.samples.assign((result.symbols.size() - 1) * sps + taps.size(), {});
    // Sparse zero-insertion convolution, retaining the complete FIR response.
    for (std::size_t k = 0; k < result.symbols.size(); ++k)
        for (std::size_t j = 0; j < taps.size(); ++j)
            result.samples[k * sps + j] += result.symbols[k] * static_cast<float>(config.amplitude_gain * taps[j]);
    finish(result);
    return result;
}
}
