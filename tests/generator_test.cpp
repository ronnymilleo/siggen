/**
 * @file    generator_test.cpp
 * @brief   Tests for symbol mapping, pulse shaping, validation and the WGN source of the generator.
 */

#include "generator.h"

#include "signal_processing.h"
#include <bit>
#include <cmath>
#include <gtest/gtest.h>
#include <numbers>
#include <set>

namespace Core {

TEST(Generator, Mapping) {
    EXPECT_EQ(MapSymbols(Modulation::BPSK, "01"), (std::vector<std::complex<float>>{{1, 0}, {-1, 0}}));
    auto qpsk = MapSymbols(Modulation::QPSK, "00011110");
    const float a = 1 / std::sqrt(2.f);
    EXPECT_EQ(qpsk, (std::vector<std::complex<float>>{{a, a}, {a, -a}, {-a, -a}, {-a, a}}));
    EXPECT_THROW(MapSymbols(Modulation::QPSK, "0"), std::invalid_argument);
    EXPECT_THROW(MapSymbols(Modulation::BPSK, "2"), std::invalid_argument);
}

TEST(Generator, DeterministicKnownSeed) {
    GenerationConfig config;
    config.SymbolCount = 5;
    auto first = Generate(config);
    EXPECT_EQ(first.Samples, Generate(config).Samples);
    // First mt19937(5489) outputs: 3499211612,581869302,3890346734,3586334585,545404204.
    EXPECT_EQ(first.Symbols, MapSymbols(Modulation::BPSK, "00010"));
    config.Seed++;
    EXPECT_NE(first.Samples, Generate(config).Samples);
}

TEST(Generator, PulseLengthDelayAndGain) {
    GenerationConfig config;
    config.SymbolCount = 1;
    auto result = Generate(config);
    auto taps = RRCFilter(config.RollOff, config.SpanSymbols, config.SamplesPerSymbol);
    ASSERT_EQ(result.Samples.size(), taps.size());
    EXPECT_EQ(result.FilterDelaySamples, 40u);
    EXPECT_DOUBLE_EQ(result.SampleRateHz, 8000);
    for (std::size_t i = 0; i < taps.size(); ++i) {
        EXPECT_FLOAT_EQ(result.Samples[i].real(), static_cast<float>(taps[i]));
    }
    config.SymbolCount = 17;
    EXPECT_EQ(Generate(config).Samples.size(), 16u * 8 + 81);
    config.Pulse = Pulse::Rectangular;
    config.DataSource = DataSource::Explicit;
    config.SymbolCount = 2;
    config.Bits = "01";
    config.AmplitudeGain = 2;
    result = Generate(config);
    EXPECT_EQ(result.Samples.size(), 16u);
    EXPECT_EQ(result.FilterDelaySamples, 0u);
    for (std::size_t i = 0; i < result.Samples.size(); ++i) {
        EXPECT_EQ(result.Samples[i], std::complex<float>(i < 8 ? 2.f : -2.f, 0));
    }
}

TEST(Generator, InvalidConfiguration) {
    GenerationConfig config;
    config.DataSource = DataSource::Explicit;
    EXPECT_THROW(Generate(config), std::invalid_argument);
    config.Bits = std::string(256, '0');
    EXPECT_NO_THROW(Generate(config));
    config.Bits += '0';
    EXPECT_THROW(Generate(config), std::invalid_argument);
    config = {};
    config.SymbolCount = 0;
    EXPECT_THROW(Generate(config), std::invalid_argument);
    config = {};
    config.SymbolRateBaud = NAN;
    EXPECT_THROW(Generate(config), std::invalid_argument);
    config = {};
    config.AmplitudeGain = INFINITY;
    EXPECT_THROW(Generate(config), std::invalid_argument);
    config = {};
    config.Modulation = static_cast<Modulation>(99);
    EXPECT_THROW(Generate(config), std::invalid_argument);
    config = {};
    config.SamplesPerSymbol = 1;
    EXPECT_THROW(Generate(config), std::invalid_argument);
}

TEST(Generator, QAM16AllPointsGrayAdjacencyAndEnergy) {
    std::string bits;
    for (unsigned word = 0; word < 16; ++word) {
        for (int b = 3; b >= 0; --b) {
            bits += (word & (1u << b)) ? '1' : '0';
        }
    }
    const auto points = MapSymbols(Modulation::QAM16, bits);
    std::set<std::pair<float, float>> unique;
    double energy = 0;
    const float levels[] = {-3, -1, 3, 1};
    for (unsigned i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(points[i].real(), levels[i >> 2] / std::sqrt(10.f));
        EXPECT_FLOAT_EQ(points[i].imag(), levels[i & 3] / std::sqrt(10.f));
        unique.emplace(points[i].real(), points[i].imag());
        energy += std::norm(points[i]);
        for (unsigned j = i + 1; j < 16; ++j) {
            if (std::abs(std::abs(points[i] - points[j]) - 2 / std::sqrt(10.f)) < 1e-6) {
                EXPECT_EQ(std::popcount(i ^ j), 1);
            }
        }
    }
    EXPECT_EQ(unique.size(), 16u);
    EXPECT_NEAR(energy / 16, 1, 1e-7);
}

TEST(Generator, PSK8AllPointsGrayAdjacencyAndEnergy) {
    std::string bits;
    for (unsigned word = 0; word < 8; ++word) {
        for (int b = 2; b >= 0; --b) {
            bits += (word & (1u << b)) ? '1' : '0';
        }
    }
    const auto points = MapSymbols(Modulation::PSK8, bits);
    ASSERT_EQ(points.size(), 8u);
    // Labels 000,001,011,010,110,111,101,100 sit at phases k*pi/4, k=0..7.
    const int phase_of_word[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    std::set<std::pair<float, float>> unique;
    double energy = 0;
    for (unsigned word = 0; word < 8; ++word) {
        const auto angle = phase_of_word[word] * std::numbers::pi / 4;
        EXPECT_NEAR(points[word].real(), std::cos(angle), 1e-6);
        EXPECT_NEAR(points[word].imag(), std::sin(angle), 1e-6);
        EXPECT_NEAR(std::norm(points[word]), 1., 1e-6);
        unique.emplace(points[word].real(), points[word].imag());
        energy += std::norm(points[word]);
    }
    EXPECT_EQ(unique.size(), 8u);
    EXPECT_NEAR(energy / 8, 1, 1e-6);
    // Nearest neighbours (angular distance pi/4, including wrap-around) differ in exactly one bit.
    for (unsigned a = 0; a < 8; ++a) {
        for (unsigned b = a + 1; b < 8; ++b) {
            const auto separation = std::abs(phase_of_word[a] - phase_of_word[b]);
            const auto distance = std::min(separation, 8 - separation);
            if (distance == 1) {
                EXPECT_EQ(std::popcount(a ^ b), 1u) << a << " vs " << b;
            }
        }
    }
    EXPECT_THROW(MapSymbols(Modulation::PSK8, "01"), std::invalid_argument);
}

TEST(Generator, QAM64AllPointsGrayAdjacencyAndEnergy) {
    std::string bits;
    for (unsigned word = 0; word < 64; ++word) {
        for (int b = 5; b >= 0; --b) {
            bits += (word & (1u << b)) ? '1' : '0';
        }
    }
    const auto points = MapSymbols(Modulation::QAM64, bits);
    ASSERT_EQ(points.size(), 64u);
    // Axis levels by three-bit label: 000,-7; 001,-5; 011,-3; 010,-1; 110,+1; 111,+3; 101,+5; 100,+7.
    const float axis[8] = {-7, -5, -1, -3, 7, 5, 1, 3};
    const float scale = 1 / std::sqrt(42.f);
    std::set<std::pair<float, float>> unique;
    double energy = 0;
    for (unsigned word = 0; word < 64; ++word) {
        EXPECT_FLOAT_EQ(points[word].real(), axis[word >> 3] * scale);
        EXPECT_FLOAT_EQ(points[word].imag(), axis[word & 7] * scale);
        unique.emplace(points[word].real(), points[word].imag());
        energy += std::norm(points[word]);
    }
    EXPECT_EQ(unique.size(), 64u);
    EXPECT_NEAR(energy / 64, 1, 1e-5);
    // Nearest neighbours (one axis step of 2/sqrt(42)) differ in exactly one bit.
    const auto step = 2 / std::sqrt(42.f);
    for (unsigned a = 0; a < 64; ++a) {
        for (unsigned b = a + 1; b < 64; ++b) {
            if (std::abs(std::abs(points[a] - points[b]) - step) < 1e-6) {
                EXPECT_EQ(std::popcount(a ^ b), 1u) << a << " vs " << b;
            }
        }
    }
    EXPECT_THROW(MapSymbols(Modulation::QAM64, "01010"), std::invalid_argument);
}

TEST(Generator, SixBitExplicitInputLimits) {
    GenerationConfig config;
    config.Modulation = Modulation::QAM64;
    config.DataSource = DataSource::Explicit;
    config.SymbolCount = 65536;
    config.Bits.assign(65536 * 6, '0');
    EXPECT_LT(config.Bits.size(), MaxExplicitBits); // Eight-bit 256-QAM sets the ceiling.
    EXPECT_NO_THROW(Validate(config));
    config.Pulse = Pulse::Rectangular;
    config.SamplesPerSymbol = 1;
    const auto maximum = Generate(config);
    EXPECT_EQ(maximum.Symbols.size(), 65536u);
    EXPECT_EQ(maximum.Samples.size(), 65536u);
    config.Bits += '0';
    EXPECT_THROW(Validate(config), std::invalid_argument);
    config.Bits.assign(MaxExplicitBits + 1, '0');
    EXPECT_THROW(Validate(config), std::invalid_argument);
    config.Bits = std::string(65535 * 6, '0');
    EXPECT_THROW(Validate(config), std::invalid_argument);
    EXPECT_THROW(MapSymbols(Modulation::WGN, "0"), std::invalid_argument);
}

TEST(Generator, FamilyTransitionsResetHiddenSettings) {
    GenerationConfig config;
    config.Awgn.Enabled = true;
    config.DataSource = DataSource::Explicit;
    config.Bits.assign(256, '1');
    config.AmplitudeGain = 2;
    config.Seed = 42;
    config.NoiseSeed = 43;
    SelectWaveform(config, Modulation::WGN);
    EXPECT_NO_THROW(Validate(config));
    EXPECT_FALSE(config.Awgn.Enabled);
    EXPECT_EQ(config.DataSource, DataSource::Random);
    EXPECT_TRUE(config.Bits.empty());
    EXPECT_EQ(config.AmplitudeGain, 2);
    EXPECT_EQ(config.Seed, 42u);
    EXPECT_EQ(config.NoiseSeed, 43u);
    config.SymbolCount = 17; // Dormant preset fields must not become active.
    SelectWaveform(config, Modulation::QAM64);
    EXPECT_EQ(config.SymbolCount, 256);
    EXPECT_NO_THROW(Validate(config));
}

TEST(Generator, WGNSourceDefaultsValidationAndDeterminism) {
    GenerationConfig config;
    config.Modulation = Modulation::WGN;
    EXPECT_NO_THROW(Validate(config));
    const auto result = Generate(config);
    EXPECT_EQ(result.Family, Family::Noise);
    EXPECT_EQ(result.Samples.size(), 2048u);
    EXPECT_DOUBLE_EQ(result.SampleRateHz, 8000);
    EXPECT_EQ(result.FilterDelaySamples, 0u);
    EXPECT_TRUE(result.Symbols.empty());
    EXPECT_EQ(Generate(config).Samples, result.Samples);
    EXPECT_EQ(result.Noise.NoiseSeed, config.NoiseSeed);
    auto other = config;
    other.NoiseSeed++;
    EXPECT_NE(Generate(other).Samples, result.Samples);
    // The data seed does not influence the noise source.
    other = config;
    other.Seed += 77;
    EXPECT_EQ(Generate(other).Samples, result.Samples);
    // Gain scales the noise amplitude; reported power includes gain squared.
    other = config;
    other.AmplitudeGain = 2;
    const auto scaled = Generate(other);
    for (std::size_t k = 0; k < result.Samples.size(); ++k) {
        EXPECT_EQ(scaled.Samples[k], result.Samples[k] * 2.f);
    }
    // Incompatible settings fail.
    other = config;
    other.Awgn.Enabled = true;
    EXPECT_THROW(Validate(other), std::invalid_argument);
    other = config;
    other.NoiseSource.SampleCount = 0;
    EXPECT_THROW(Validate(other), std::invalid_argument);
    other = config;
    other.NoiseSource.SampleRateHz = 0;
    EXPECT_THROW(Validate(other), std::invalid_argument);
    other = config;
    other.NoiseSource.NoisePower = -1;
    EXPECT_THROW(Validate(other), std::invalid_argument);
    other = config;
    other.NoiseSource.NoisePower = NAN;
    EXPECT_THROW(Validate(other), std::invalid_argument);
}

} // namespace Core
