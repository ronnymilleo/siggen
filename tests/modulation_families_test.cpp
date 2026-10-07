/**
 * @file    modulation_families_test.cpp
 * @brief   Tests for OOK, 4-PAM, 256-QAM, differential PSK and FSK/MSK generation, analysis and integration.
 */

#include "batch.h"

#include "impairments.h"
#include "iq_export.h"
#include "measurements.h"
#include "preset.h"
#include "signal_analysis.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <gtest/gtest.h>
#include <numbers>
#include <random>
#include <set>
#include <string>

namespace Core {

namespace {

using Sym = std::complex<float>;

// Bit label of value, most significant bit first
std::string Label(unsigned value, int bits) {
    std::string text;
    for (int k = bits - 1; k >= 0; --k) {
        text += ((value >> k) & 1u) ? '1' : '0';
    }
    return text;
}

void ExpectNear(Sym actual, Sym expected, float tolerance = 1e-6f) {
    EXPECT_NEAR(actual.real(), expected.real(), tolerance);
    EXPECT_NEAR(actual.imag(), expected.imag(), tolerance);
}

// Full constellation of a memoryless mapper, one point per bit label
std::vector<Sym> Constellation(Modulation modulation) {
    const int bits_per_symbol = BitsPerSymbol(modulation);
    std::vector<Sym> points;
    for (unsigned v = 0; v < (1u << bits_per_symbol); ++v) {
        points.push_back(MapSymbols(modulation, Label(v, bits_per_symbol))[0]);
    }
    return points;
}

double MeanEnergy(const std::vector<Sym> &points) {
    double total = 0;
    for (auto point : points) {
        total += std::norm(point);
    }
    return total / static_cast<double>(points.size());
}

} // namespace

TEST(Pam, OokAndFourPamMappings) {
    EXPECT_EQ(MapSymbols(Modulation::OOK, "01"), (std::vector<Sym>{{0, 0}, {std::sqrt(2.f), 0}}));
    EXPECT_NEAR(MeanEnergy(Constellation(Modulation::OOK)), 1, 1e-6);
    const auto pam = Constellation(Modulation::PAM4);
    const float scale = 1 / std::sqrt(5.f);
    // Labels 00,01,11,10 ascend -3,-1,+1,+3.
    ExpectNear(pam[0], {-3 * scale, 0});
    ExpectNear(pam[1], {-1 * scale, 0});
    ExpectNear(pam[3], {1 * scale, 0});
    ExpectNear(pam[2], {3 * scale, 0});
    EXPECT_NEAR(MeanEnergy(pam), 1, 1e-6);
    for (auto point : pam) {
        EXPECT_EQ(point.imag(), 0);
    }
    // Neighbouring amplitudes differ in exactly one bit.
    std::vector<std::pair<float, unsigned>> by_level;
    for (unsigned v = 0; v < 4; ++v) {
        by_level.emplace_back(pam[v].real(), v);
    }
    std::sort(by_level.begin(), by_level.end());
    for (std::size_t k = 1; k < by_level.size(); ++k) {
        EXPECT_EQ(std::popcount(by_level[k].second ^ by_level[k - 1].second), 1);
    }
}

TEST(Qam256, FullConstellationIsGrayAndUnitEnergy) {
    const auto points = Constellation(Modulation::QAM256);
    ASSERT_EQ(points.size(), 256u);
    EXPECT_NEAR(MeanEnergy(points), 1, 1e-6);
    std::set<std::pair<int, int>> seen;
    const double unit = 2 / std::sqrt(170.);
    for (auto point : points) {
        const auto i = std::lround(point.real() / unit * 1000), q = std::lround(point.imag() / unit * 1000);
        seen.emplace(static_cast<int>(i), static_cast<int>(q));
    }
    EXPECT_EQ(seen.size(), 256u);
    for (unsigned a = 0; a < 256; ++a) {
        for (unsigned b = a + 1; b < 256; ++b) {
            if (std::abs(std::abs(points[a] - points[b]) - unit) < 1e-5) {
                EXPECT_EQ(std::popcount(a ^ b), 1) << a << " " << b;
            }
        }
    }
    // First four bits select I: 0000 is the most negative level, 1000 the most positive.
    ExpectNear(points[0b00000000], {-15 * static_cast<float>(unit / 2), -15 * static_cast<float>(unit / 2)});
    ExpectNear(points[0b10001000], {15 * static_cast<float>(unit / 2), 15 * static_cast<float>(unit / 2)});
    EXPECT_THROW(MapSymbols(Modulation::QAM256, "1010101"), std::invalid_argument);
}

TEST(Qam256, MaximumExplicitInputAndEvm) {
    GenerationConfig config;
    config.Modulation = Modulation::QAM256;
    config.DataSource = DataSource::Explicit;
    config.SymbolCount = 65536;
    config.Bits.assign(65536 * 8, '0');
    EXPECT_EQ(config.Bits.size(), MaxExplicitBits);
    EXPECT_NO_THROW(Validate(config));
    config.Bits += '0';
    EXPECT_THROW(Validate(config), std::invalid_argument);
    GenerationConfig clean;
    clean.Modulation = Modulation::QAM256;
    clean.SymbolCount = 256;
    const auto accuracy = MeasureSymbolAccuracy(Generate(clean));
    ASSERT_TRUE(accuracy);
    EXPECT_LT(accuracy->EvmRms, 0.01);
}

TEST(Differential, DbpskAccumulatesPhaseFlips) {
    // Bit 1 flips the carrier phase, bit 0 keeps it; the reference phase is zero.
    EXPECT_EQ(MapSymbols(Modulation::DBPSK, "0110101"),
              (std::vector<Sym>{{1, 0}, {-1, 0}, {1, 0}, {1, 0}, {-1, 0}, {-1, 0}, {1, 0}}));
}

TEST(Differential, DqpskGrayIncrementsAndRotationInvariance) {
    const float a = 1 / std::sqrt(2.f);
    // Reference phase 45 degrees; increments 00 -> 0, 01 -> +90, 11 -> +180, 10 -> +270.
    const auto mapped = MapSymbols(Modulation::DQPSK, "00"
                                                      "01"
                                                      "11"
                                                      "10"
                                                      "10");
    ExpectNear(mapped[0], {a, a});
    ExpectNear(mapped[1], {-a, a});
    ExpectNear(mapped[2], {a, -a});
    ExpectNear(mapped[3], {-a, -a});
    ExpectNear(mapped[4], {-a, a});
    // Decoding with symbols[k] * conj(symbols[k-1]) recovers the increment, so a constant
    // channel rotation does not change the decoded data.
    GenerationConfig config;
    config.Modulation = Modulation::DQPSK;
    config.SymbolCount = 200;
    const auto symbols = Generate(config).Symbols;
    const auto rotation = std::polar(1.f, 0.7f);
    const char *label_by_turn[4] = {"00", "01", "11", "10"}; // Quarter turns 0..3.
    std::string decoded, decoded_rotated;
    for (std::size_t k = 1; k < symbols.size(); ++k) {
        const auto step = std::arg(symbols[k] * std::conj(symbols[k - 1]));
        const auto rotated = std::arg(symbols[k] * rotation * std::conj(symbols[k - 1] * rotation));
        const auto index = [](float angle) {
            return (static_cast<int>(std::lround(angle / (std::numbers::pi / 2))) + 4) % 4;
        };
        decoded += label_by_turn[index(step)];
        decoded_rotated += label_by_turn[index(rotated)];
    }
    EXPECT_EQ(decoded, decoded_rotated);
    // The decoded increments reproduce the transmitted random bits (from symbol 1 on).
    std::mt19937 rng(config.Seed);
    std::string sent(config.SymbolCount * 2, '0');
    for (char &bit : sent) {
        bit = (rng() & 1u) ? '1' : '0';
    }
    EXPECT_EQ(decoded, sent.substr(2));
}

TEST(Differential, NewLinearModulationsShapeAndMeasure) {
    for (auto modulation :
         {Modulation::OOK, Modulation::PAM4, Modulation::DBPSK, Modulation::DQPSK, Modulation::QAM256}) {
        GenerationConfig config;
        config.Modulation = modulation;
        config.SymbolCount = 128;
        const auto result = Generate(config);
        EXPECT_EQ(result.Samples.size(), 127u * 8 + 81) << ModulationName(modulation);
        EXPECT_EQ(result.Symbols.size(), 128u);
        const auto accuracy = MeasureSymbolAccuracy(result);
        ASSERT_TRUE(accuracy) << ModulationName(modulation);
        EXPECT_LT(accuracy->EvmRms, 0.02) << ModulationName(modulation);
        EXPECT_NO_THROW(BuildEyeDiagram(result));
    }
}

TEST(Fsk, DescriptorsAndValidation) {
    for (auto modulation : {Modulation::FSK2, Modulation::FSK4, Modulation::MSK}) {
        EXPECT_EQ(WaveformFamily(modulation), Family::Fsk);
    }
    EXPECT_EQ(BitsPerSymbol(Modulation::FSK4), 2);
    GenerationConfig config;
    config.Modulation = Modulation::FSK2;
    config.Pulse = Pulse::Rectangular;
    EXPECT_NO_THROW(Validate(config));
    config.SamplesPerSymbol = 1; // Tones at +/-500 Hz against Nyquist 500 Hz.
    EXPECT_THROW(Validate(config), std::invalid_argument);
    config.SamplesPerSymbol = 2; // 500 Hz < 1000 Hz.
    EXPECT_NO_THROW(Validate(config));
    config.Modulation = Modulation::FSK4; // Outer tones at +/-1500 Hz, Nyquist 1000 Hz.
    EXPECT_THROW(Validate(config), std::invalid_argument);
    config.SamplesPerSymbol = 4;
    EXPECT_NO_THROW(Validate(config));
    config.ToneSpacingHz = 0;
    EXPECT_THROW(Validate(config), std::invalid_argument);
    config.ToneSpacingHz = std::nan("");
    EXPECT_THROW(Validate(config), std::invalid_argument);
    // RRC settings are dormant for FSK: SPS 1 is fine for MSK even with the default pulse.
    GenerationConfig msk;
    msk.Modulation = Modulation::MSK;
    msk.SamplesPerSymbol = 1;
    EXPECT_NO_THROW(Validate(msk));
    // Explicit input is exact length, bits per symbol follow the modulation.
    GenerationConfig explicit_bits;
    explicit_bits.Modulation = Modulation::FSK4;
    explicit_bits.DataSource = DataSource::Explicit;
    explicit_bits.SymbolCount = 3;
    explicit_bits.Bits = "000111";
    EXPECT_NO_THROW(Validate(explicit_bits));
    explicit_bits.Bits = "00011";
    EXPECT_THROW(Validate(explicit_bits), std::invalid_argument);
    EXPECT_THROW(MapSymbols(Modulation::FSK2, "0"), std::invalid_argument);
    // Selecting FSK from a linear family resets dormant linear settings but keeps gain and seeds.
    GenerationConfig linear;
    linear.AmplitudeGain = 3;
    linear.Seed = 7;
    linear.SymbolCount = 11;
    SelectWaveform(linear, Modulation::FSK2);
    EXPECT_EQ(linear.AmplitudeGain, 3);
    EXPECT_EQ(linear.Seed, 7u);
    EXPECT_EQ(linear.SymbolCount, 256);
    SelectWaveform(linear, Modulation::MSK);
    EXPECT_EQ(WaveformFamily(linear.Modulation), Family::Fsk);
}

TEST(Fsk, WaveformMatchesIndependentPhaseFixture) {
    GenerationConfig config;
    config.Modulation = Modulation::FSK4;
    config.DataSource = DataSource::Explicit;
    config.SymbolCount = 4;
    config.Bits = "00011110"; // Ascending labels 00,01,11,10 -> tones -1.5,-0.5,+0.5,+1.5 spacings.
    config.SymbolRateBaud = 1000;
    config.SamplesPerSymbol = 16;
    config.ToneSpacingHz = 600;
    config.AmplitudeGain = 2;
    const auto result = Generate(config);
    EXPECT_EQ(result.Family, Family::Fsk);
    EXPECT_TRUE(result.Symbols.empty());
    EXPECT_EQ(result.FilterDelaySamples, 0u);
    EXPECT_DOUBLE_EQ(result.SampleRateHz, 16000);
    ASSERT_EQ(result.Samples.size(), 64u);
    EXPECT_EQ(result.SymbolFrequenciesHz, (std::vector<double>{-900, -300, 300, 900}));
    double phase = 0; // Total phase computed without wrapping, as a running sum.
    for (std::size_t n = 0; n < result.Samples.size(); ++n) {
        EXPECT_NEAR(result.Samples[n].real(), 2 * std::cos(phase), 2e-5) << n;
        EXPECT_NEAR(result.Samples[n].imag(), 2 * std::sin(phase), 2e-5) << n;
        EXPECT_NEAR(std::abs(result.Samples[n]), 2, 2e-6);
        phase += 2 * std::numbers::pi * result.SymbolFrequenciesHz[n / 16] / 16000;
    }
    // The frequency estimate recovers each tone in the interior of every symbol.
    const auto estimate = InstantaneousFrequency(result.Samples, result.SampleRateHz);
    ASSERT_EQ(estimate.size(), result.Samples.size() - 1);
    for (std::size_t n = 0; n + 1 < result.Samples.size(); ++n) {
        EXPECT_NEAR(estimate[n], result.SymbolFrequenciesHz[n / 16], 0.5) << n;
    }
}

TEST(Fsk, ContinuousPhaseAtSymbolBoundaries) {
    GenerationConfig config;
    config.Modulation = Modulation::FSK2;
    config.SymbolCount = 64;
    config.SamplesPerSymbol = 8;
    const auto result = Generate(config);
    ASSERT_EQ(result.Samples.size(), 512u);
    // With continuous phase, the step across a boundary is a tone step too (never a jump).
    const auto estimate = InstantaneousFrequency(result.Samples, result.SampleRateHz);
    for (double f : estimate) {
        EXPECT_NEAR(std::abs(f), 500, 0.5);
    }
    EXPECT_NEAR(std::abs(result.Samples[0]), 1, 1e-6);
    EXPECT_EQ(result.Samples[0], Sym(1, 0)); // Zero initial phase.
    EXPECT_NEAR(MeasurePowerStatistics(result.Samples).PaprDb, 0, 1e-4);
}

TEST(Fsk, MskIsBinaryFskWithHalfModulationIndex) {
    GenerationConfig config;
    config.Modulation = Modulation::MSK;
    config.SymbolRateBaud = 2000;
    config.SamplesPerSymbol = 8;
    config.ToneSpacingHz = 12345; // Ignored: MSK locks the spacing.
    config.DataSource = DataSource::Explicit;
    config.SymbolCount = 4;
    config.Bits = "0110";
    EXPECT_DOUBLE_EQ(FskModulationIndex(config), 0.5);
    EXPECT_DOUBLE_EQ(FskToneSpacingHz(config), 1000);
    const auto result = Generate(config);
    EXPECT_EQ(result.SymbolFrequenciesHz, (std::vector<double>{-500, 500, 500, -500})); // +/- baud/4.
    // Over one symbol the phase moves by +/- pi/2, as in the MSK definition.
    const auto &samples = result.Samples;
    EXPECT_NEAR(std::arg(samples[8] * std::conj(samples[0])), -std::numbers::pi / 2, 1e-5);
    EXPECT_NEAR(std::arg(samples[16] * std::conj(samples[8])), std::numbers::pi / 2, 1e-5);
    // Equals 2-FSK with spacing baud/2 and the same bits.
    GenerationConfig fsk = config;
    fsk.Modulation = Modulation::FSK2;
    fsk.ToneSpacingHz = 1000;
    EXPECT_EQ(Generate(fsk).Samples, result.Samples);
    EXPECT_DOUBLE_EQ(FskModulationIndex(fsk), 0.5);
}

TEST(Fsk, AnalysisViewsDoNotApplyToFsk) {
    GenerationConfig config;
    config.Modulation = Modulation::FSK2;
    const auto result = Generate(config);
    EXPECT_FALSE(MeasureSymbolAccuracy(result));
    EXPECT_THROW(MatchedSymbols(result), std::invalid_argument);
    EXPECT_THROW(BuildEyeDiagram(result), std::invalid_argument);
    EXPECT_NO_THROW(WelchPsd(result.Samples, result.SampleRateHz));
}

TEST(Fsk, SpectrumConcentratesAtTheTones) {
    GenerationConfig config;
    config.Modulation = Modulation::FSK2;
    config.SymbolCount = 1024;
    config.SamplesPerSymbol = 8;
    const auto result = Generate(config);
    const auto psd = WelchPsd(result.Samples, result.SampleRateHz, 1024, Window::Hann);
    std::size_t peak = 0;
    for (std::size_t k = 0; k < psd.PowerDensity.size(); ++k) {
        if (psd.PowerDensity[k] > psd.PowerDensity[peak]) {
            peak = k;
        }
    }
    EXPECT_NEAR(std::abs(psd.FrequencyHz[peak]), 500, 20);
}

TEST(Fsk, AwgnImpairmentsAndSeeds) {
    GenerationConfig config;
    config.Modulation = Modulation::FSK2;
    config.SymbolCount = 128;
    const auto clean = Generate(config);
    config.Awgn.Enabled = true;
    config.Awgn.SnrDb = 10;
    config.Impairments.CfoHz = 25;
    const auto noisy = Generate(config);
    EXPECT_TRUE(noisy.Noise.AwgnApplied);
    EXPECT_TRUE(noisy.ImpairmentsApplied);
    EXPECT_EQ(noisy.Noise.ReferenceBegin, 0u);
    EXPECT_EQ(noisy.Noise.ReferenceEnd, noisy.Samples.size());
    EXPECT_NEAR(noisy.Noise.ReferencePower, 1, 1e-5);
    EXPECT_NE(noisy.Samples, clean.Samples);
    EXPECT_EQ(Generate(config).Samples, noisy.Samples);
    // Noise and impairments never change the transmitted tones.
    EXPECT_EQ(noisy.SymbolFrequenciesHz, clean.SymbolFrequenciesHz);
}

TEST(Fsk, PresetRoundTripUsesVersionFour) {
    GenerationConfig config;
    config.Modulation = Modulation::FSK4;
    config.ToneSpacingHz = 750;
    config.SamplesPerSymbol = 8;
    config.Impairments.CfoHz = 3;
    const auto text = SerializePreset(config);
    EXPECT_NE(text.find("Version=4"), std::string::npos);
    EXPECT_NE(text.find("ToneSpacingHz=750"), std::string::npos);
    EXPECT_EQ(ParsePreset(text), config);
    config.Impairments = {};
    EXPECT_EQ(ParsePreset(SerializePreset(config)), config);
    // FSK needs the v4 format; linear and noise presets keep their older versions.
    auto bad = text;
    bad.replace(bad.find("Version=4"), 9, "Version=3");
    EXPECT_THROW(ParsePreset(bad), std::invalid_argument);
    GenerationConfig linear;
    linear.Modulation = Modulation::DQPSK;
    EXPECT_NE(SerializePreset(linear).find("Version=2"), std::string::npos);
    EXPECT_EQ(ParsePreset(SerializePreset(linear)), linear);
    // Version 1 presets still cannot carry newer waveforms.
    auto v1 = SerializePreset(GenerationConfig{});
    v1.replace(v1.find("Version=2"), 9, "Version=1");
    v1.replace(v1.find("Modulation=BPSK"), 15, "Modulation=OOK");
    EXPECT_THROW(ParsePreset(v1), std::invalid_argument);
}

TEST(Fsk, ExportMetadataAndFrames) {
    GenerationConfig config;
    config.Modulation = Modulation::FSK2;
    config.SymbolCount = 16;
    const auto result = Generate(config);
    const auto metadata = ExportMetadata(result, ExportFormat::CSV);
    EXPECT_NE(metadata.find("\"family\": \"fsk\""), std::string::npos);
    EXPECT_NE(metadata.find("\"tone_spacing_hz\": 1000"), std::string::npos);
    EXPECT_NE(metadata.find("\"modulation_index\": 1"), std::string::npos);
    EXPECT_EQ(metadata.find("\"pulse\""), std::string::npos);
    auto broken = result;
    broken.SymbolFrequenciesHz.pop_back();
    EXPECT_THROW(ExportMetadata(broken, ExportFormat::CSV), std::invalid_argument);
    // Batch frames are exact-length, unguarded slices of the continuous-phase signal.
    config.SymbolCount = 256;
    const auto frame = GenerateFrame(config, 100, 0, std::nullopt);
    EXPECT_EQ(frame.Samples.size(), 100u);
    EXPECT_EQ(frame.CropOffset, 0u);
    EXPECT_EQ(frame.FilterDelaySamples, 0u);
    EXPECT_EQ(frame.Samples[0], Sym(1, 0));
    for (auto sample : frame.Samples) {
        EXPECT_NEAR(std::abs(sample), 1, 1e-6);
    }
    EXPECT_NO_THROW(ValidateFrameRequest(config, Modulation::MSK, 4096));
    EXPECT_NO_THROW(ValidateFrameRequest(config, Modulation::QAM256, 4096));
}

} // namespace Core
