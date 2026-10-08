/**
 * @file    generator.h
 * @brief   Generation settings, the generated signal and the functions that validate settings and generate it.
 */

#ifndef SIGGEN_GENERATOR_H
#define SIGGEN_GENERATOR_H

#include "impairments.h"
#include "waveform.h"
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace Core {

// Explicit-count limit: eight bits per symbol (256-QAM) at up to 65536 symbols
inline constexpr std::size_t MaxExplicitBits = 65536 * 8;

/**
 * @struct  NoiseSourceSettings
 * @brief   WGN source settings; active only for the noise family.
 */
struct NoiseSourceSettings {
    int SampleCount = 2048;
    double SampleRateHz = 8000;
    double NoisePower = 1; // Total complex power before amplitude gain
    bool operator==(const NoiseSourceSettings &) const = default;
};

/**
 * @struct  AwgnSettings
 * @brief   Optional AWGN overlay; active only for linear families when enabled.
 */
struct AwgnSettings {
    bool Enabled = false;
    double SnrDb = 10; // Clean sample power divided by added complex noise power
    bool operator==(const AwgnSettings &) const = default;
};

/**
 * @struct  GenerationConfig
 * @brief   Every setting of one generation, for all waveform families.
 * @details Settings of the families the selected waveform does not use stay dormant but are still validated.
 */
struct GenerationConfig {
    Core::Modulation Modulation = Core::Modulation::BPSK;
    // Linear-modulation settings (ignored for noise sources)
    int SymbolCount = 256;
    double SymbolRateBaud = 1000;
    int SamplesPerSymbol = 8;
    Core::Pulse Pulse = Core::Pulse::RRC;
    double RollOff = 0.2;
    int SpanSymbols = 10;
    // FSK settings (2-FSK and 4-FSK; MSK locks the spacing to symbol rate / 2). Adjacent tones are
    // ToneSpacingHz apart, so h = ToneSpacingHz / SymbolRateBaud
    double ToneSpacingHz = 1000;
    // Common output and data settings
    double AmplitudeGain = 1;
    Core::DataSource DataSource = Core::DataSource::Random;
    std::uint32_t Seed = 5489;
    std::string Bits;
    // Noise family and AWGN overlay; the default noise seed is shared and fixed
    NoiseSourceSettings NoiseSource;
    AwgnSettings Awgn;
    std::uint32_t NoiseSeed = 5490;
    // Channel impairments after AWGN; linear families only, off by default
    ImpairmentSettings Impairments;
    std::uint32_t ImpairmentSeed = 5491;
    bool operator==(const GenerationConfig &) const = default;
};

/**
 * @struct  NoiseRecord
 * @brief   Provenance of any noise contribution, recorded for metadata.
 */
struct NoiseRecord {
    bool AwgnApplied = false;
    double RequestedSnrDb = 0;
    double ReferencePower = 0; // Mean clean complex power over the reference interval
    std::size_t ReferenceBegin = 0;
    std::size_t ReferenceEnd = 0;
    double AddedNoisePower = 0; // Target total complex noise power
    std::uint32_t NoiseSeed = 0;
    bool operator==(const NoiseRecord &) const = default;
};

/**
 * @struct  GeneratedSignal
 * @brief   The samples of one generation, with the data, settings and noise provenance behind them.
 */
struct GeneratedSignal {
    std::vector<std::complex<float>> Samples;
    std::vector<std::complex<float>> Symbols; // Unit-energy mapping, before gain; empty for noise and FSK sources
    std::vector<double> SymbolFrequenciesHz;  // Tone of each symbol (FSK family only)
    std::string Bits;                         // Transmitted bits, '0'/'1' (empty for noise sources)
    GenerationConfig Config;
    Core::Family Family = Core::Family::Linear;
    double SampleRateHz = 0;
    std::size_t FilterDelaySamples = 0;
    NoiseRecord Noise;
    bool ImpairmentsApplied = false;
};

// Derived settings
bool UsesRrc(const GenerationConfig &config);
std::size_t QuadratureDelaySamples(const GenerationConfig &config);
double FskToneSpacingHz(const GenerationConfig &config);
double FskModulationIndex(const GenerationConfig &config);

// Settings
void SelectWaveform(GenerationConfig &config, Modulation modulation);
void Validate(const GenerationConfig &config);

// Generation
std::vector<std::complex<float>> MapSymbols(Modulation modulation, const std::string &bits);
GeneratedSignal Generate(const GenerationConfig &config);

} // namespace Core

#endif // SIGGEN_GENERATOR_H
