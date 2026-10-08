/**
 * @file    generator.cpp
 * @brief   Generation settings, the generated signal and the functions that validate settings and generate it.
 */

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

namespace Core {

namespace {

// 8-PSK: phases k*pi/4 counterclockwise carry labels 000,001,011,010,110,111,101,100
constexpr int Psk8PhaseIndex[8] = {0, 1, 3, 2, 7, 6, 4, 5};
// 64-QAM axis levels by three-bit Gray label, ascending amplitude order, /sqrt(42)
constexpr int Qam64AxisLevel[8] = {-7, -5, -1, -3, 7, 5, 1, 3};
// 32-QAM cross, odd-integer (x, y) of the eight points of the first quadrant (the 3x3 grid without its corner
// (5,5)), indexed by the three-bit label value. The grid has a point with four neighbours, so a full Gray
// labelling does not exist; this assignment (found by exhaustive search) minimises the worst Hamming distance
// between nearest neighbours: eight of the ten in-quadrant neighbour pairs differ in one bit, the other two in two
constexpr int Qam32QuadrantPoint[8][2] = {{1, 1}, {3, 1}, {5, 1}, {5, 3}, {1, 3}, {3, 3}, {1, 5}, {3, 5}};

// region Bit labels

// Inverse of the binary-reflected Gray code: label -> ascending level index
int GrayIndex(int label) {
    int index = label;
    for (int shift = 1; (label >> shift) > 0; ++shift) {
        index ^= label >> shift;
    }
    return index;
}

// Gray-labelled amplitude on an axis of 2^bits levels: odd integers -(2^bits-1)..+(2^bits-1)
int GrayLevel(int label, int bits) {
    return 2 * GrayIndex(label) - ((1 << bits) - 1);
}

// Reads `count` bits starting at `offset` as an unsigned integer, most significant bit first
int BitsValue(const std::string &bits, std::size_t offset, int count) {
    int value = 0;
    for (int k = 0; k < count; ++k) {
        value = (value << 1) | (bits[offset + static_cast<std::size_t>(k)] == '1' ? 1 : 0);
    }
    return value;
}

// endregion

// region Validation

void ValidateCommonSettings(const GenerationConfig &config) {
    if (!std::isfinite(config.AmplitudeGain) || config.AmplitudeGain < 0 || config.AmplitudeGain > 1000000) {
        throw std::invalid_argument("Amplitude gain must be finite and in [0,1000000]");
    }
    if (config.DataSource != DataSource::Random && config.DataSource != DataSource::Explicit) {
        throw std::invalid_argument("Unsupported data source");
    }
    if (config.Bits.size() > MaxExplicitBits) {
        throw std::invalid_argument("Bit input exceeds limit");
    }
}

void ValidateTimingAndPulse(const GenerationConfig &config, const WaveformDescriptor &descriptor) {
    if (config.SymbolCount < 1 || config.SymbolCount > 65536) {
        throw std::invalid_argument("Symbol count must be 1–65536");
    }
    if (config.SamplesPerSymbol < 1 || config.SamplesPerSymbol > 32) {
        throw std::invalid_argument("Samples per symbol must be 1–32");
    }
    if (!std::isfinite(config.SymbolRateBaud) || config.SymbolRateBaud <= 0 ||
        !std::isfinite(config.SymbolRateBaud * config.SamplesPerSymbol) ||
        !std::isfinite(MaxSignalSamples / (config.SymbolRateBaud * config.SamplesPerSymbol))) {
        throw std::invalid_argument("Symbol rate must be positive with finite sample rate and duration");
    }
    if (config.Pulse != Pulse::RRC && config.Pulse != Pulse::Rectangular) {
        throw std::invalid_argument("Unsupported pulse");
    }
    if (!std::isfinite(config.RollOff) || config.RollOff < 0 || config.RollOff > 1 || config.SpanSymbols < 1 ||
        config.SpanSymbols > 100) {
        throw std::invalid_argument("Roll-off must be [0,1] and span must be 1–100 symbols");
    }
    if (descriptor.Family != Family::Fsk && config.Pulse == Pulse::RRC &&
        (config.SamplesPerSymbol < 2 || (config.SpanSymbols * config.SamplesPerSymbol) % 2)) {
        throw std::invalid_argument("RRC needs SPS >= 2 and even span * SPS");
    }
    const auto sps = static_cast<std::size_t>(config.SamplesPerSymbol);
    if (config.Modulation == Modulation::OQPSK && (config.SamplesPerSymbol < 2 || config.SamplesPerSymbol % 2)) {
        throw std::invalid_argument(
            "OQPSK needs an even samples per symbol (the quadrature stream lags by half a symbol)");
    }
    const auto tail = (UsesRrc(config) ? static_cast<std::size_t>(config.SpanSymbols) * sps + 1 : sps) +
                      QuadratureDelaySamples(config);
    if (static_cast<std::size_t>(config.SymbolCount - 1) > (MaxSignalSamples - tail) / sps) {
        throw std::length_error("Generation exceeds sample limit");
    }
}

void ValidateNoiseSource(const NoiseSourceSettings &noise_source) {
    if (noise_source.SampleCount < 1 || static_cast<std::size_t>(noise_source.SampleCount) > MaxSignalSamples) {
        throw std::invalid_argument("Noise sample count must be 1–4194304");
    }
    if (!std::isfinite(noise_source.SampleRateHz) || noise_source.SampleRateHz <= 0 ||
        !std::isfinite(MaxSignalSamples / noise_source.SampleRateHz)) {
        throw std::invalid_argument("Noise sample rate must be positive with finite duration");
    }
    if (!std::isfinite(noise_source.NoisePower) || noise_source.NoisePower < 0) {
        throw std::invalid_argument("Noise power must be finite and non-negative");
    }
}

void ValidateAwgnAndImpairments(const GenerationConfig &config) {
    if (!std::isfinite(config.Awgn.SnrDb)) {
        throw std::invalid_argument("AWGN SNR must be finite");
    }
    Validate(config.Impairments);
}

void ValidateFsk(const GenerationConfig &config, const WaveformDescriptor &descriptor) {
    if (!std::isfinite(config.ToneSpacingHz) || config.ToneSpacingHz <= 0) {
        throw std::invalid_argument("Tone spacing must be positive and finite");
    }
    if (descriptor.Family == Family::Fsk) {
        // Every tone centre must lie strictly inside Nyquist; the spectral skirts of rectangular frequency
        // transitions may still alias at low SPS
        const auto outer = (static_cast<double>(1 << descriptor.BitsPerSymbol) - 1) / 2 * FskToneSpacingHz(config);
        if (!(outer < config.SymbolRateBaud * config.SamplesPerSymbol / 2)) {
            throw std::invalid_argument("FSK tone centres must lie strictly below half the sample rate");
        }
    }
}

// Settings that only make sense for the active family: noise sources reject AWGN, impairments and explicit bits;
// the other families check the explicit bits against the symbol count
void ValidateFamilySpecific(const GenerationConfig &config, const WaveformDescriptor &descriptor) {
    if (descriptor.Family == Family::Noise) {
        if (config.Impairments.Active()) {
            throw std::invalid_argument("Impairments do not apply to noise sources");
        }
        if (!descriptor.AwgnSupported && config.Awgn.Enabled) {
            throw std::invalid_argument("AWGN does not apply to noise sources");
        }
        if (!descriptor.ExplicitInput && config.DataSource == DataSource::Explicit) {
            throw std::invalid_argument("Noise sources use seeded random generation");
        }
        return;
    }
    const auto bits_per_symbol = static_cast<std::size_t>(descriptor.BitsPerSymbol);
    if (config.DataSource == DataSource::Explicit &&
        (config.Bits.size() != static_cast<std::size_t>(config.SymbolCount) * bits_per_symbol ||
         config.Bits.find_first_not_of("01") != std::string::npos)) {
        throw std::invalid_argument("Explicit input must contain exactly symbol count * bits per symbol binary digits");
    }
}

// endregion

// region Symbol mappers

// Each mapper reads the bits of one symbol starting at `offset` and returns its unit-energy constellation point;
// differential mappers also advance `phase_steps`, the phase accumulated from the data increments

std::complex<float> MapBpsk(const std::string &bits, std::size_t offset) {
    return {bits[offset] == '0' ? 1.f : -1.f, 0.f};
}

std::complex<float> MapQpsk(const std::string &bits, std::size_t offset) {
    const float in_phase = bits[offset] == '0' ? 1.f : -1.f;
    return {in_phase / std::sqrt(2.f), (bits[offset + 1] == '0' ? 1.f : -1.f) / std::sqrt(2.f)};
}

std::complex<float> MapPsk8(const std::string &bits, std::size_t offset) {
    const auto phase_index = Psk8PhaseIndex[BitsValue(bits, offset, 3)];
    const auto angle = static_cast<double>(phase_index) * std::numbers::pi / 4;
    return {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
}

std::complex<float> MapQam16(const std::string &bits, std::size_t offset) {
    // Axis pairs 00 -> -3, 01 -> -1, 11 -> +1, 10 -> +3
    const auto level = [&](std::size_t k) {
        return (bits[k] == '0' ? -1.f : 1.f) * (bits[k + 1] == '0' ? 3.f : 1.f) / std::sqrt(10.f);
    };
    return {level(offset), level(offset + 2)};
}

std::complex<float> MapQam32(const std::string &bits, std::size_t offset) {
    // Cross constellation: a 6x6 grid without its four corners, unit mean energy (sum 640 / 32 = 20). Bits 0-1
    // pick the quadrant (00 +,+ / 01 -,+ / 11 -,- / 10 +,-), bits 2-4 the point within it; the point labels are
    // mirrored per quadrant so neighbours across an axis differ in one bit
    const auto quadrant = BitsValue(bits, offset, 2);
    const auto &point = Qam32QuadrantPoint[BitsValue(bits, offset + 2, 3)];
    const float sign_x = (quadrant == 1 || quadrant == 3) ? -1.f : 1.f;
    const float sign_y = (quadrant == 3 || quadrant == 2) ? -1.f : 1.f;
    const auto scale = 1.f / std::sqrt(20.f);
    return {sign_x * static_cast<float>(point[0]) * scale, sign_y * static_cast<float>(point[1]) * scale};
}

std::complex<float> MapQam64(const std::string &bits, std::size_t offset) {
    const auto scale = 1.f / std::sqrt(42.f);
    return {Qam64AxisLevel[BitsValue(bits, offset, 3)] * scale, Qam64AxisLevel[BitsValue(bits, offset + 3, 3)] * scale};
}

std::complex<float> MapQam256(const std::string &bits, std::size_t offset) {
    const auto scale = 1.f / std::sqrt(170.f);
    return {static_cast<float>(GrayLevel(BitsValue(bits, offset, 4), 4)) * scale,
            static_cast<float>(GrayLevel(BitsValue(bits, offset + 4, 4), 4)) * scale};
}

std::complex<float> MapPam4(const std::string &bits, std::size_t offset) {
    return {static_cast<float>(GrayLevel(BitsValue(bits, offset, 2), 2)) / std::sqrt(5.f), 0.f};
}

std::complex<float> MapOok(const std::string &bits, std::size_t offset) {
    // sqrt(2) for a one keeps the mean energy at 1
    return {bits[offset] == '1' ? std::sqrt(2.f) : 0.f, 0.f};
}

std::complex<float> MapAsk4(const std::string &bits, std::size_t offset) {
    // Unipolar levels 0..3 by Gray label 00, 01, 11, 10; mean energy (0+1+4+9)/4 = 3.5
    return {static_cast<float>(GrayIndex(BitsValue(bits, offset, 2))) / std::sqrt(3.5f), 0.f};
}

std::complex<float> MapDbpsk(const std::string &bits, std::size_t offset, int &phase_steps) {
    phase_steps ^= bits[offset] == '1' ? 1 : 0;
    return {phase_steps ? -1.f : 1.f, 0.f};
}

std::complex<float> MapDqpsk(const std::string &bits, std::size_t offset, int &phase_steps) {
    // Gray phase increments 00 -> 0, 01 -> +90, 11 -> +180, 10 -> +270 degrees, in quarter turns modulo 4
    constexpr int Increment[4] = {0, 1, 3, 2};
    phase_steps = (phase_steps + Increment[BitsValue(bits, offset, 2)]) % 4;
    static constexpr float SignI[4] = {1.f, -1.f, -1.f, 1.f}, SignQ[4] = {1.f, 1.f, -1.f, -1.f};
    const float scale = 1.f / std::sqrt(2.f);
    return {SignI[phase_steps] * scale, SignQ[phase_steps] * scale};
}

std::complex<float> MapPi4Dqpsk(const std::string &bits, std::size_t offset, int &phase_steps) {
    // Gray phase increments 00 -> +45, 01 -> +135, 11 -> -135, 10 -> -45 degrees, in steps of pi/4 modulo 8;
    // indexed by the label value 00, 01, 10, 11
    constexpr int Increment[4] = {1, 3, 7, 5};
    phase_steps = (phase_steps + Increment[BitsValue(bits, offset, 2)]) % 8;
    const auto angle = static_cast<double>(phase_steps) * std::numbers::pi / 4;
    return {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
}

std::complex<float> MapDpsk8(const std::string &bits, std::size_t offset, int &phase_steps) {
    // Gray phase increments: labels 000, 001, 011, 010, 110, 111, 101, 100 turn the phase by 0..7 steps of pi/4
    phase_steps = (phase_steps + Psk8PhaseIndex[BitsValue(bits, offset, 3)]) % 8;
    const auto angle = static_cast<double>(phase_steps) * std::numbers::pi / 4;
    return {static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
}

std::complex<float> MapSymbol(Modulation modulation, const std::string &bits, std::size_t offset, int &phase_steps) {
    switch (modulation) {
    case Modulation::BPSK:
        return MapBpsk(bits, offset);
    case Modulation::QPSK:
    case Modulation::OQPSK:
        return MapQpsk(bits, offset);
    case Modulation::PSK8:
        return MapPsk8(bits, offset);
    case Modulation::QAM16:
        return MapQam16(bits, offset);
    case Modulation::QAM32:
        return MapQam32(bits, offset);
    case Modulation::QAM64:
        return MapQam64(bits, offset);
    case Modulation::QAM256:
        return MapQam256(bits, offset);
    case Modulation::PAM4:
        return MapPam4(bits, offset);
    case Modulation::OOK:
        return MapOok(bits, offset);
    case Modulation::ASK4:
        return MapAsk4(bits, offset);
    case Modulation::DBPSK:
        return MapDbpsk(bits, offset, phase_steps);
    case Modulation::DQPSK:
        return MapDqpsk(bits, offset, phase_steps);
    case Modulation::PI4DQPSK:
        return MapPi4Dqpsk(bits, offset, phase_steps);
    case Modulation::DPSK8:
        return MapDpsk8(bits, offset, phase_steps);
    case Modulation::WGN:
    case Modulation::FSK2:
    case Modulation::FSK4:
    case Modulation::MSK:
        break;
    }
    throw std::invalid_argument("Waveform has no implemented symbol mapper");
}

// endregion

// region Generation

// Clean steady-state reference interval for AWGN, excluding RRC transients
std::pair<std::size_t, std::size_t> AwgnReferenceInterval(const GenerationConfig &config, std::size_t sample_count) {
    const auto sps = static_cast<std::size_t>(config.SamplesPerSymbol);
    if (UsesRrc(config)) {
        const auto begin = static_cast<std::size_t>(config.SpanSymbols) * sps;
        const auto end = static_cast<std::size_t>(config.SymbolCount) * sps;
        if (begin >= end || end > sample_count) {
            throw std::invalid_argument("AWGN reference interval is empty");
        }
        return {begin, end};
    }
    if (sample_count == 0) {
        throw std::invalid_argument("AWGN reference interval is empty");
    }
    return {0, sample_count};
}

void ApplyAwgn(GeneratedSignal &result) {
    const auto [begin, end] = AwgnReferenceInterval(result.Config, result.Samples.size());
    result.Noise = AddAwgn(result.Samples, begin, end, result.Config.Awgn.SnrDb, result.Config.NoiseSeed);
}

// AWGN, then channel impairments, for every non-noise family
void Finish(GeneratedSignal &result) {
    const auto &config = result.Config;
    if (config.Awgn.Enabled) {
        ApplyAwgn(result);
    }
    if (config.Impairments.Active()) {
        ApplyImpairments(result.Samples, result.SampleRateHz, config.Impairments, config.ImpairmentSeed);
        result.ImpairmentsApplied = true;
    }
}

void GenerateNoise(GeneratedSignal &result) {
    const auto &config = result.Config;
    result.SampleRateHz = config.NoiseSource.SampleRateHz;
    result.Samples = GaussianNoise(static_cast<std::size_t>(config.NoiseSource.SampleCount),
                                   config.NoiseSource.NoisePower, config.NoiseSeed);
    if (config.AmplitudeGain != 1) {
        for (auto &sample : result.Samples) {
            sample *= static_cast<float>(config.AmplitudeGain);
        }
    }
    result.Noise = NoiseRecord{false, 0, 0, 0, 0, config.NoiseSource.NoisePower, config.NoiseSeed};
}

std::string RandomBits(const GenerationConfig &config) {
    std::string bits(static_cast<std::size_t>(config.SymbolCount) *
                         static_cast<std::size_t>(BitsPerSymbol(config.Modulation)),
                     '\0');
    std::mt19937 engine(config.Seed);
    for (char &bit : bits) {
        // One engine output per bit, least significant bit
        bit = (engine() & 1u) ? '1' : '0';
    }
    return bits;
}

// Continuous-phase FSK: phase[n+1] = phase[n] + 2 pi f[n] / Fs, starting at zero.
// Labels ascend in tone: 0,1 (2-FSK) and 00,01,11,10 (4-FSK)
void GenerateFsk(GeneratedSignal &result, const std::string &bits) {
    const auto &config = result.Config;
    const auto bits_per_symbol = static_cast<std::size_t>(BitsPerSymbol(config.Modulation));
    const auto sps = static_cast<std::size_t>(config.SamplesPerSymbol);
    const auto spacing = FskToneSpacingHz(config);
    const auto levels = static_cast<double>(1 << bits_per_symbol);
    result.Samples.resize(static_cast<std::size_t>(config.SymbolCount) * sps);
    result.SymbolFrequenciesHz.reserve(static_cast<std::size_t>(config.SymbolCount));
    constexpr double TwoPi = 2 * std::numbers::pi;
    double phase = 0;
    for (std::size_t k = 0; k < static_cast<std::size_t>(config.SymbolCount); ++k) {
        const auto tone = GrayIndex(BitsValue(bits, k * bits_per_symbol, static_cast<int>(bits_per_symbol)));
        const auto frequency = (static_cast<double>(tone) - (levels - 1) / 2) * spacing;
        result.SymbolFrequenciesHz.push_back(frequency);
        const auto increment = TwoPi * frequency / result.SampleRateHz;
        for (std::size_t j = 0; j < sps; ++j) {
            result.Samples[k * sps + j] =
                std::complex<float>(static_cast<float>(config.AmplitudeGain * std::cos(phase)),
                                    static_cast<float>(config.AmplitudeGain * std::sin(phase)));
            phase = std::remainder(phase + increment, TwoPi);
        }
    }
}

// Sparse zero-insertion convolution of the mapped symbols with the pulse, retaining the complete FIR response
void ShapeSymbols(GeneratedSignal &result) {
    const auto &config = result.Config;
    const auto sps = static_cast<std::size_t>(config.SamplesPerSymbol);
    std::vector<double> taps;
    if (config.Pulse == Pulse::RRC) {
        taps = RRCFilter(config.RollOff, config.SpanSymbols, config.SamplesPerSymbol);
        result.FilterDelaySamples = (taps.size() - 1) / 2;
    } else {
        taps.assign(sps, 1.);
    }
    const auto quadrature_delay = QuadratureDelaySamples(config);
    result.Samples.assign((result.Symbols.size() - 1) * sps + taps.size() + quadrature_delay, {});
    for (std::size_t k = 0; k < result.Symbols.size(); ++k) {
        for (std::size_t j = 0; j < taps.size(); ++j) {
            const auto weight = static_cast<float>(config.AmplitudeGain * taps[j]);
            if (quadrature_delay == 0) {
                result.Samples[k * sps + j] += result.Symbols[k] * weight;
            } else {
                // OQPSK: the quadrature stream lags the in-phase stream by half a symbol
                result.Samples[k * sps + j] += std::complex<float>(result.Symbols[k].real() * weight, 0);
                result.Samples[k * sps + quadrature_delay + j] +=
                    std::complex<float>(0, result.Symbols[k].imag() * weight);
            }
        }
    }
}

// endregion

} // namespace

/**
 * @brief   Tells whether the signal is a linear symbol train shaped by an RRC pulse.
 * @param[in] config  Generation settings.
 * @return  True for a linear family with the RRC pulse.
 */
bool UsesRrc(const GenerationConfig &config) {
    return WaveformFamily(config.Modulation) == Family::Linear && config.Pulse == Pulse::RRC;
}

/**
 * @brief   Returns the samples by which the quadrature stream lags the in-phase stream.
 * @param[in] config  Generation settings.
 * @return  Half a symbol for OQPSK, zero otherwise. The buffer is lengthened by the same amount.
 */
std::size_t QuadratureDelaySamples(const GenerationConfig &config) {
    return config.Modulation == Modulation::OQPSK ? static_cast<std::size_t>(config.SamplesPerSymbol) / 2 : 0;
}

/**
 * @brief   Returns the effective FSK tone spacing.
 * @param[in] config  Generation settings.
 * @return  The configured spacing in Hz, or symbol rate / 2 for MSK.
 */
double FskToneSpacingHz(const GenerationConfig &config) {
    return config.Modulation == Modulation::MSK ? config.SymbolRateBaud * MskModulationIndex : config.ToneSpacingHz;
}

/**
 * @brief   Returns the FSK modulation index.
 * @param[in] config  Generation settings.
 * @return  h = tone spacing / symbol rate.
 */
double FskModulationIndex(const GenerationConfig &config) {
    return FskToneSpacingHz(config) / config.SymbolRateBaud;
}

/**
 * @brief   Switches the settings to another waveform.
 * @param[in,out] config      Settings to change.
 * @param[in]     modulation  Waveform to select.
 * @note    A family change resets the dormant settings to their defaults, keeping the amplitude gain and the
 *          seeds; FSK starts with the rectangular pulse.
 */
void SelectWaveform(GenerationConfig &config, Modulation modulation) {
    if (WaveformFamily(config.Modulation) != WaveformFamily(modulation)) {
        GenerationConfig defaults;
        defaults.AmplitudeGain = config.AmplitudeGain;
        defaults.Seed = config.Seed;
        if (WaveformFamily(modulation) == Family::Fsk) {
            defaults.Pulse = Pulse::Rectangular;
        }
        defaults.NoiseSeed = config.NoiseSeed;
        defaults.ImpairmentSeed = config.ImpairmentSeed;
        config = defaults;
    }
    config.Modulation = modulation;
}

/**
 * @brief   Checks every generation setting.
 * @param[in] config  Settings to check.
 * @note    Throws std::invalid_argument for the first invalid setting, or std::length_error when the signal
 *          would exceed MaxSignalSamples. Persisted fields are always validated, even when the active waveform
 *          family ignores them, so presets fail transactionally.
 */
void Validate(const GenerationConfig &config) {
    if (!IsValid(config.Modulation)) {
        throw std::invalid_argument("Unsupported modulation");
    }
    const auto &descriptor = GetWaveformDescriptor(config.Modulation);
    ValidateCommonSettings(config);
    ValidateTimingAndPulse(config, descriptor);
    ValidateNoiseSource(config.NoiseSource);
    ValidateAwgnAndImpairments(config);
    ValidateFsk(config, descriptor);
    ValidateFamilySpecific(config, descriptor);
}

/**
 * @brief   Maps bits to the unit-energy constellation points of a linear waveform.
 * @param[in] modulation  A linear waveform.
 * @param[in] bits        '0'/'1' characters, a whole number of symbols and at most 65536 of them.
 * @return  One point per symbol, before amplitude gain. Differential schemes start from phase zero.
 * @note    Throws std::invalid_argument for a non-linear waveform or invalid bits.
 */
std::vector<std::complex<float>> MapSymbols(Modulation modulation, const std::string &bits) {
    const auto &descriptor = GetWaveformDescriptor(modulation);
    if (descriptor.Family != Family::Linear) {
        throw std::invalid_argument("Only linear waveforms have a symbol mapping");
    }
    const auto bits_per_symbol = static_cast<std::size_t>(descriptor.BitsPerSymbol);
    if (bits.size() > 65536 * bits_per_symbol || bits.size() % bits_per_symbol ||
        bits.find_first_not_of("01") != std::string::npos) {
        throw std::invalid_argument("Invalid mapper bit input");
    }
    std::vector<std::complex<float>> symbols;
    symbols.reserve(bits.size() / bits_per_symbol);
    int phase_steps = 0;
    for (std::size_t offset = 0; offset < bits.size(); offset += bits_per_symbol) {
        symbols.push_back(MapSymbol(modulation, bits, offset, phase_steps));
    }
    return symbols;
}

/**
 * @brief   Generates the samples of a signal.
 * @param[in] config  Generation settings; validated first.
 * @return  The samples with the bits, symbols and noise provenance behind them.
 * @note    Deterministic: the same settings always give the same samples. Random bits take the least
 *          significant bit of one mt19937 output per bit, seeded with config.Seed.
 */
GeneratedSignal Generate(const GenerationConfig &config) {
    Validate(config);
    GeneratedSignal result;
    result.Config = config;
    result.Family = WaveformFamily(config.Modulation);
    if (result.Family == Family::Noise) {
        GenerateNoise(result);
        return result;
    }
    const std::string bits = config.DataSource == DataSource::Random ? RandomBits(config) : config.Bits;
    result.Bits = bits;
    result.SampleRateHz = config.SymbolRateBaud * config.SamplesPerSymbol;
    if (result.Family == Family::Fsk) {
        GenerateFsk(result, bits);
        Finish(result);
        return result;
    }
    result.Symbols = MapSymbols(config.Modulation, bits);
    ShapeSymbols(result);
    Finish(result);
    return result;
}

} // namespace Core
