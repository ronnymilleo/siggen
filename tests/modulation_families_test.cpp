#include "batch.h"
#include "impairments.h"
#include "iq_export.h"
#include "measurements.h"
#include "preset.h"
#include "signal_analysis.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <set>
#include <string>

using namespace iq;

namespace {
std::string label(unsigned value, int bits) {
    std::string text;
    for (int k = bits - 1; k >= 0; --k) text += ((value >> k) & 1u) ? '1' : '0';
    return text;
}
using Sym = std::complex<float>;
void expect_near(Sym a, Sym b, float tolerance = 1e-6f) {
    EXPECT_NEAR(a.real(), b.real(), tolerance);
    EXPECT_NEAR(a.imag(), b.imag(), tolerance);
}
// Full constellation of a memoryless mapper, one point per bit label.
std::vector<Sym> constellation(Modulation m) {
    const int bps = bits_per_symbol(m);
    std::vector<Sym> points;
    for (unsigned v = 0; v < (1u << bps); ++v) points.push_back(map_symbols(m, label(v, bps))[0]);
    return points;
}
double mean_energy(const std::vector<Sym>& points) {
    double total = 0;
    for (auto p : points) total += std::norm(p);
    return total / static_cast<double>(points.size());
}
}

TEST(Pam, OokAndFourPamMappings) {
    EXPECT_EQ(map_symbols(Modulation::OOK, "01"), (std::vector<Sym>{{0, 0}, {std::sqrt(2.f), 0}}));
    EXPECT_NEAR(mean_energy(constellation(Modulation::OOK)), 1, 1e-6);
    const auto pam = constellation(Modulation::PAM4);
    const float s = 1 / std::sqrt(5.f);
    // Labels 00,01,11,10 ascend -3,-1,+1,+3.
    expect_near(pam[0], {-3 * s, 0});
    expect_near(pam[1], {-1 * s, 0});
    expect_near(pam[3], {1 * s, 0});
    expect_near(pam[2], {3 * s, 0});
    EXPECT_NEAR(mean_energy(pam), 1, 1e-6);
    for (auto p : pam) EXPECT_EQ(p.imag(), 0);
    // Neighbouring amplitudes differ in exactly one bit.
    std::vector<std::pair<float, unsigned>> by_level;
    for (unsigned v = 0; v < 4; ++v) by_level.emplace_back(pam[v].real(), v);
    std::sort(by_level.begin(), by_level.end());
    for (std::size_t k = 1; k < by_level.size(); ++k)
        EXPECT_EQ(std::popcount(by_level[k].second ^ by_level[k - 1].second), 1);
}

TEST(Qam256, FullConstellationIsGrayAndUnitEnergy) {
    const auto points = constellation(Modulation::QAM256);
    ASSERT_EQ(points.size(), 256u);
    EXPECT_NEAR(mean_energy(points), 1, 1e-6);
    std::set<std::pair<int, int>> seen;
    const double unit = 2 / std::sqrt(170.);
    for (auto p : points) {
        const auto i = std::lround(p.real() / unit * 1000), q = std::lround(p.imag() / unit * 1000);
        seen.emplace(static_cast<int>(i), static_cast<int>(q));
    }
    EXPECT_EQ(seen.size(), 256u);
    for (unsigned a = 0; a < 256; ++a)
        for (unsigned b = a + 1; b < 256; ++b)
            if (std::abs(std::abs(points[a] - points[b]) - unit) < 1e-5) {
                EXPECT_EQ(std::popcount(a ^ b), 1) << a << " " << b;
            }
    // First four bits select I: 0000 is the most negative level, 1000 the most positive.
    expect_near(points[0b00000000], {-15 * static_cast<float>(unit / 2), -15 * static_cast<float>(unit / 2)});
    expect_near(points[0b10001000], {15 * static_cast<float>(unit / 2), 15 * static_cast<float>(unit / 2)});
    EXPECT_THROW(map_symbols(Modulation::QAM256, "1010101"), std::invalid_argument);
}

TEST(Qam256, MaximumExplicitInputAndEvm) {
    GenerationConfig c;
    c.modulation = Modulation::QAM256;
    c.data_source = DataSource::Explicit;
    c.symbol_count = 65536;
    c.bits.assign(65536 * 8, '0');
    EXPECT_EQ(c.bits.size(), MAX_EXPLICIT_BITS);
    EXPECT_NO_THROW(validate(c));
    c.bits += '0';
    EXPECT_THROW(validate(c), std::invalid_argument);
    GenerationConfig clean;
    clean.modulation = Modulation::QAM256;
    clean.symbol_count = 256;
    const auto accuracy = symbol_accuracy(generate(clean));
    ASSERT_TRUE(accuracy);
    EXPECT_LT(accuracy->evm_rms, 0.01);
}

TEST(Differential, DbpskAccumulatesPhaseFlips) {
    // Bit 1 flips the carrier phase, bit 0 keeps it; the reference phase is zero.
    EXPECT_EQ(map_symbols(Modulation::DBPSK, "0110101"),
              (std::vector<Sym>{{1, 0}, {-1, 0}, {1, 0}, {1, 0}, {-1, 0}, {-1, 0}, {1, 0}}));
}

TEST(Differential, DqpskGrayIncrementsAndRotationInvariance) {
    const float a = 1 / std::sqrt(2.f);
    // Reference phase 45 degrees; increments 00 -> 0, 01 -> +90, 11 -> +180, 10 -> +270.
    const auto s = map_symbols(Modulation::DQPSK, "00" "01" "11" "10" "10");
    expect_near(s[0], {a, a});
    expect_near(s[1], {-a, a});
    expect_near(s[2], {a, -a});
    expect_near(s[3], {-a, -a});
    expect_near(s[4], {-a, a});
    // Decoding with s[k] * conj(s[k-1]) recovers the increment, so a constant
    // channel rotation does not change the decoded data.
    GenerationConfig c;
    c.modulation = Modulation::DQPSK;
    c.symbol_count = 200;
    const auto symbols = generate(c).symbols;
    const auto rotation = std::polar(1.f, 0.7f);
    const char* label_by_turn[4] = {"00", "01", "11", "10"}; // Quarter turns 0..3.
    std::string decoded, decoded_rotated;
    for (std::size_t k = 1; k < symbols.size(); ++k) {
        const auto step = std::arg(symbols[k] * std::conj(symbols[k - 1]));
        const auto rotated = std::arg(symbols[k] * rotation * std::conj(symbols[k - 1] * rotation));
        const auto index = [](float angle) { return (static_cast<int>(std::lround(angle / (std::numbers::pi / 2))) + 4) % 4; };
        decoded += label_by_turn[index(step)];
        decoded_rotated += label_by_turn[index(rotated)];
    }
    EXPECT_EQ(decoded, decoded_rotated);
    // The decoded increments reproduce the transmitted random bits (from symbol 1 on).
    std::mt19937 rng(c.seed);
    std::string sent(c.symbol_count * 2, '0');
    for (char& bit : sent) bit = (rng() & 1u) ? '1' : '0';
    EXPECT_EQ(decoded, sent.substr(2));
}

TEST(Differential, NewLinearModulationsShapeAndMeasure) {
    for (auto m : {Modulation::OOK, Modulation::PAM4, Modulation::DBPSK, Modulation::DQPSK, Modulation::QAM256}) {
        GenerationConfig c;
        c.modulation = m;
        c.symbol_count = 128;
        const auto r = generate(c);
        EXPECT_EQ(r.samples.size(), 127u * 8 + 81) << modulation_name(m);
        EXPECT_EQ(r.symbols.size(), 128u);
        const auto accuracy = symbol_accuracy(r);
        ASSERT_TRUE(accuracy) << modulation_name(m);
        EXPECT_LT(accuracy->evm_rms, 0.02) << modulation_name(m);
        EXPECT_NO_THROW(eye_diagram(r));
    }
}

TEST(Fsk, DescriptorsAndValidation) {
    for (auto m : {Modulation::FSK2, Modulation::FSK4, Modulation::MSK}) EXPECT_EQ(waveform_family(m), Family::Fsk);
    EXPECT_EQ(bits_per_symbol(Modulation::FSK4), 2);
    GenerationConfig c;
    c.modulation = Modulation::FSK2;
    c.pulse = Pulse::Rectangular;
    EXPECT_NO_THROW(validate(c));
    c.samples_per_symbol = 1; // Tones at +/-500 Hz against Nyquist 500 Hz.
    EXPECT_THROW(validate(c), std::invalid_argument);
    c.samples_per_symbol = 2; // 500 Hz < 1000 Hz.
    EXPECT_NO_THROW(validate(c));
    c.modulation = Modulation::FSK4; // Outer tones at +/-1500 Hz, Nyquist 1000 Hz.
    EXPECT_THROW(validate(c), std::invalid_argument);
    c.samples_per_symbol = 4;
    EXPECT_NO_THROW(validate(c));
    c.tone_spacing_hz = 0;
    EXPECT_THROW(validate(c), std::invalid_argument);
    c.tone_spacing_hz = std::nan("");
    EXPECT_THROW(validate(c), std::invalid_argument);
    // RRC settings are dormant for FSK: SPS 1 is fine for MSK even with the default pulse.
    GenerationConfig msk;
    msk.modulation = Modulation::MSK;
    msk.samples_per_symbol = 1;
    EXPECT_NO_THROW(validate(msk));
    // Explicit input is exact length, bits per symbol follow the modulation.
    GenerationConfig explicit_bits;
    explicit_bits.modulation = Modulation::FSK4;
    explicit_bits.data_source = DataSource::Explicit;
    explicit_bits.symbol_count = 3;
    explicit_bits.bits = "000111";
    EXPECT_NO_THROW(validate(explicit_bits));
    explicit_bits.bits = "00011";
    EXPECT_THROW(validate(explicit_bits), std::invalid_argument);
    EXPECT_THROW(map_symbols(Modulation::FSK2, "0"), std::invalid_argument);
    // Selecting FSK from a linear family resets dormant linear settings but keeps gain and seeds.
    GenerationConfig linear;
    linear.amplitude_gain = 3;
    linear.seed = 7;
    linear.symbol_count = 11;
    select_waveform(linear, Modulation::FSK2);
    EXPECT_EQ(linear.amplitude_gain, 3);
    EXPECT_EQ(linear.seed, 7u);
    EXPECT_EQ(linear.symbol_count, 256);
    select_waveform(linear, Modulation::MSK);
    EXPECT_EQ(waveform_family(linear.modulation), Family::Fsk);
}

TEST(Fsk, WaveformMatchesIndependentPhaseFixture) {
    GenerationConfig c;
    c.modulation = Modulation::FSK4;
    c.data_source = DataSource::Explicit;
    c.symbol_count = 4;
    c.bits = "00011110"; // Ascending labels 00,01,11,10 -> tones -1.5,-0.5,+0.5,+1.5 spacings.
    c.symbol_rate_baud = 1000;
    c.samples_per_symbol = 16;
    c.tone_spacing_hz = 600;
    c.amplitude_gain = 2;
    const auto r = generate(c);
    EXPECT_EQ(r.family, Family::Fsk);
    EXPECT_TRUE(r.symbols.empty());
    EXPECT_EQ(r.filter_delay_samples, 0u);
    EXPECT_DOUBLE_EQ(r.sample_rate_hz, 16000);
    ASSERT_EQ(r.samples.size(), 64u);
    EXPECT_EQ(r.symbol_frequencies_hz, (std::vector<double>{-900, -300, 300, 900}));
    double phase = 0; // Total phase computed without wrapping, as a running sum.
    for (std::size_t n = 0; n < r.samples.size(); ++n) {
        EXPECT_NEAR(r.samples[n].real(), 2 * std::cos(phase), 2e-5) << n;
        EXPECT_NEAR(r.samples[n].imag(), 2 * std::sin(phase), 2e-5) << n;
        EXPECT_NEAR(std::abs(r.samples[n]), 2, 2e-6);
        phase += 2 * std::numbers::pi * r.symbol_frequencies_hz[n / 16] / 16000;
    }
    // The frequency estimate recovers each tone in the interior of every symbol.
    const auto estimate = instantaneous_frequency(r.samples, r.sample_rate_hz);
    ASSERT_EQ(estimate.size(), r.samples.size() - 1);
    for (std::size_t n = 0; n + 1 < r.samples.size(); ++n)
        EXPECT_NEAR(estimate[n], r.symbol_frequencies_hz[n / 16], 0.5) << n;
}

TEST(Fsk, ContinuousPhaseAtSymbolBoundaries) {
    GenerationConfig c;
    c.modulation = Modulation::FSK2;
    c.symbol_count = 64;
    c.samples_per_symbol = 8;
    const auto r = generate(c);
    ASSERT_EQ(r.samples.size(), 512u);
    // With continuous phase, the step across a boundary is a tone step too (never a jump).
    const auto estimate = instantaneous_frequency(r.samples, r.sample_rate_hz);
    for (double f : estimate) EXPECT_NEAR(std::abs(f), 500, 0.5);
    EXPECT_NEAR(std::abs(r.samples[0]), 1, 1e-6);
    EXPECT_EQ(r.samples[0], Sym(1, 0)); // Zero initial phase.
    EXPECT_NEAR(power_statistics(r.samples).papr_db, 0, 1e-4);
}

TEST(Fsk, MskIsBinaryFskWithHalfModulationIndex) {
    GenerationConfig c;
    c.modulation = Modulation::MSK;
    c.symbol_rate_baud = 2000;
    c.samples_per_symbol = 8;
    c.tone_spacing_hz = 12345; // Ignored: MSK locks the spacing.
    c.data_source = DataSource::Explicit;
    c.symbol_count = 4;
    c.bits = "0110";
    EXPECT_DOUBLE_EQ(fsk_modulation_index(c), 0.5);
    EXPECT_DOUBLE_EQ(fsk_tone_spacing_hz(c), 1000);
    const auto r = generate(c);
    EXPECT_EQ(r.symbol_frequencies_hz, (std::vector<double>{-500, 500, 500, -500})); // +/- baud/4.
    // Over one symbol the phase moves by +/- pi/2, as in the MSK definition.
    const auto& s = r.samples;
    EXPECT_NEAR(std::arg(s[8] * std::conj(s[0])), -std::numbers::pi / 2, 1e-5);
    EXPECT_NEAR(std::arg(s[16] * std::conj(s[8])), std::numbers::pi / 2, 1e-5);
    // Equals 2-FSK with spacing baud/2 and the same bits.
    GenerationConfig fsk = c;
    fsk.modulation = Modulation::FSK2;
    fsk.tone_spacing_hz = 1000;
    EXPECT_EQ(generate(fsk).samples, r.samples);
    EXPECT_DOUBLE_EQ(fsk_modulation_index(fsk), 0.5);
}

TEST(Fsk, AnalysisViewsDoNotApplyToFsk) {
    GenerationConfig c;
    c.modulation = Modulation::FSK2;
    const auto r = generate(c);
    EXPECT_FALSE(symbol_accuracy(r));
    EXPECT_THROW(matched_symbols(r), std::invalid_argument);
    EXPECT_THROW(eye_diagram(r), std::invalid_argument);
    EXPECT_NO_THROW(welch_psd(r.samples, r.sample_rate_hz));
}

TEST(Fsk, SpectrumConcentratesAtTheTones) {
    GenerationConfig c;
    c.modulation = Modulation::FSK2;
    c.symbol_count = 1024;
    c.samples_per_symbol = 8;
    const auto r = generate(c);
    const auto psd = welch_psd(r.samples, r.sample_rate_hz, 1024, Window::Hann);
    std::size_t peak = 0;
    for (std::size_t k = 0; k < psd.power_density.size(); ++k)
        if (psd.power_density[k] > psd.power_density[peak]) peak = k;
    EXPECT_NEAR(std::abs(psd.frequency_hz[peak]), 500, 20);
}

TEST(Fsk, AwgnImpairmentsAndSeeds) {
    GenerationConfig c;
    c.modulation = Modulation::FSK2;
    c.symbol_count = 128;
    const auto clean = generate(c);
    c.awgn.enabled = true;
    c.awgn.snr_db = 10;
    c.impairments.cfo_hz = 25;
    const auto noisy = generate(c);
    EXPECT_TRUE(noisy.noise.awgn_applied);
    EXPECT_TRUE(noisy.impairments_applied);
    EXPECT_EQ(noisy.noise.reference_begin, 0u);
    EXPECT_EQ(noisy.noise.reference_end, noisy.samples.size());
    EXPECT_NEAR(noisy.noise.reference_power, 1, 1e-5);
    EXPECT_NE(noisy.samples, clean.samples);
    EXPECT_EQ(generate(c).samples, noisy.samples);
    // Noise and impairments never change the transmitted tones.
    EXPECT_EQ(noisy.symbol_frequencies_hz, clean.symbol_frequencies_hz);
}

TEST(Fsk, PresetRoundTripUsesVersionFour) {
    GenerationConfig c;
    c.modulation = Modulation::FSK4;
    c.tone_spacing_hz = 750;
    c.samples_per_symbol = 8;
    c.impairments.cfo_hz = 3;
    const auto text = serialize_preset(c);
    EXPECT_NE(text.find("Version=4"), std::string::npos);
    EXPECT_NE(text.find("ToneSpacingHz=750"), std::string::npos);
    EXPECT_EQ(parse_preset(text), c);
    c.impairments = {};
    EXPECT_EQ(parse_preset(serialize_preset(c)), c);
    // FSK needs the v4 format; linear and noise presets keep their older versions.
    auto bad = text;
    bad.replace(bad.find("Version=4"), 9, "Version=3");
    EXPECT_THROW(parse_preset(bad), std::invalid_argument);
    GenerationConfig linear;
    linear.modulation = Modulation::DQPSK;
    EXPECT_NE(serialize_preset(linear).find("Version=2"), std::string::npos);
    EXPECT_EQ(parse_preset(serialize_preset(linear)), linear);
    // Version 1 presets still cannot carry newer waveforms.
    auto v1 = serialize_preset(GenerationConfig{});
    v1.replace(v1.find("Version=2"), 9, "Version=1");
    v1.replace(v1.find("Modulation=BPSK"), 15, "Modulation=OOK");
    EXPECT_THROW(parse_preset(v1), std::invalid_argument);
}

TEST(Fsk, ExportMetadataAndFrames) {
    GenerationConfig c;
    c.modulation = Modulation::FSK2;
    c.symbol_count = 16;
    const auto r = generate(c);
    const auto metadata = export_metadata(r, ExportFormat::CSV);
    EXPECT_NE(metadata.find("\"family\": \"fsk\""), std::string::npos);
    EXPECT_NE(metadata.find("\"tone_spacing_hz\": 1000"), std::string::npos);
    EXPECT_NE(metadata.find("\"modulation_index\": 1"), std::string::npos);
    EXPECT_EQ(metadata.find("\"pulse\""), std::string::npos);
    auto broken = r;
    broken.symbol_frequencies_hz.pop_back();
    EXPECT_THROW(export_metadata(broken, ExportFormat::CSV), std::invalid_argument);
    // Batch frames are exact-length, unguarded slices of the continuous-phase signal.
    c.symbol_count = 256;
    const auto frame = generate_frame(c, 100, 0, std::nullopt);
    EXPECT_EQ(frame.samples.size(), 100u);
    EXPECT_EQ(frame.crop_offset, 0u);
    EXPECT_EQ(frame.filter_delay_samples, 0u);
    EXPECT_EQ(frame.samples[0], Sym(1, 0));
    for (auto sample : frame.samples) EXPECT_NEAR(std::abs(sample), 1, 1e-6);
    EXPECT_NO_THROW(validate_frame_request(c, Modulation::MSK, 4096));
    EXPECT_NO_THROW(validate_frame_request(c, Modulation::QAM256, 4096));
}
