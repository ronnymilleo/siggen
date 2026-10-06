#pragma once
#include "impairments.h"
#include "waveform.h"
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace iq {
// Explicit-count limit: eight bits per symbol (256-QAM) at up to 65536 symbols.
inline constexpr std::size_t MAX_EXPLICIT_BITS = 65536 * 8;
// WGN source settings; active only for the noise family.
struct NoiseSourceSettings {
    int sample_count = 2048;
    double sample_rate_hz = 8000;
    double noise_power = 1; // Total complex power before amplitude gain.
    bool operator==(const NoiseSourceSettings&) const = default;
};
// Optional AWGN overlay; active only for linear families when enabled.
struct AwgnSettings {
    bool enabled = false;
    double snr_db = 10; // Clean sample power divided by added complex noise power.
    bool operator==(const AwgnSettings&) const = default;
};
struct GenerationConfig {
    Modulation modulation = Modulation::BPSK;
    // Linear-modulation settings (ignored for noise sources).
    int symbol_count = 256;
    double symbol_rate_baud = 1000;
    int samples_per_symbol = 8;
    Pulse pulse = Pulse::RRC;
    double roll_off = 0.2;
    int span_symbols = 10;
    // FSK settings (2-FSK and 4-FSK; MSK locks the spacing to symbol rate / 2).
    // Adjacent tones are tone_spacing_hz apart, so h = tone_spacing_hz / symbol_rate_baud.
    double tone_spacing_hz = 1000;
    // Common output/data settings.
    double amplitude_gain = 1;
    DataSource data_source = DataSource::Random;
    std::uint32_t seed = 5489;
    std::string bits;
    // Noise family and AWGN overlay. Default noise seed is shared and fixed.
    NoiseSourceSettings noise_source;
    AwgnSettings awgn;
    std::uint32_t noise_seed = 5490;
    // Channel impairments after AWGN; linear families only, off by default.
    ImpairmentSettings impairments;
    std::uint32_t impairment_seed = 5491;
    bool operator==(const GenerationConfig&) const = default;
};
// Provenance of any noise contribution, recorded for metadata.
struct NoiseRecord {
    bool awgn_applied = false;
    double requested_snr_db = 0;
    double reference_power = 0; // Mean clean complex power over the reference interval.
    std::size_t reference_begin = 0;
    std::size_t reference_end = 0;
    double added_noise_power = 0; // Target total complex noise power.
    std::uint32_t noise_seed = 0;
    bool operator==(const NoiseRecord&) const = default;
};
struct GeneratedSignal {
    std::vector<std::complex<float>> samples;
    std::vector<std::complex<float>> symbols; // Unit-energy mapping, before gain. Empty for noise and FSK sources.
    std::vector<double> symbol_frequencies_hz; // Tone of each symbol (FSK family only).
    GenerationConfig config;
    Family family = Family::Linear;
    double sample_rate_hz = 0;
    std::size_t filter_delay_samples = 0;
    NoiseRecord noise;
    bool impairments_applied = false;
};
// True when the signal is a linear symbol train shaped by an RRC pulse.
bool uses_rrc(const GenerationConfig& config);
// Samples by which the quadrature stream lags the in-phase stream: half a
// symbol for OQPSK, zero otherwise. It lengthens the buffer by the same amount.
std::size_t quadrature_delay_samples(const GenerationConfig& config);
// Effective tone spacing: the configured value, or symbol rate / 2 for MSK.
double fsk_tone_spacing_hz(const GenerationConfig& config);
// Modulation index h = tone spacing / symbol rate.
double fsk_modulation_index(const GenerationConfig& config);
void validate(const GenerationConfig& config);
// A family change resets dormant settings, retaining common gain and seeds.
void select_waveform(GenerationConfig& config, Modulation modulation);
std::vector<std::complex<float>> map_symbols(Modulation modulation, const std::string& bits);
GeneratedSignal generate(const GenerationConfig& config);
}
