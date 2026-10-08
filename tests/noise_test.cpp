/**
 * @file    noise_test.cpp
 * @brief   Tests for the seeded Gaussian noise source, the WGN waveform and AWGN at a requested SNR.
 */

#include "noise.h"

#include "generator.h"
#include "signal_analysis.h"
#include <cmath>
#include <gtest/gtest.h>
#include <numbers>
#include <random>

namespace Core {

namespace {

// Sample mean and variance of each component and the I/Q correlation coefficient
struct Statistics {
    double MeanI = 0, MeanQ = 0, VarI = 0, VarQ = 0, Correlation = 0;
};

Statistics ComponentStatistics(const std::vector<std::complex<float>> &samples) {
    Statistics statistics;
    const auto count = static_cast<double>(samples.size());
    for (const auto &value : samples) {
        statistics.MeanI += value.real();
        statistics.MeanQ += value.imag();
    }
    statistics.MeanI /= count;
    statistics.MeanQ /= count;
    double cross = 0;
    for (const auto &value : samples) {
        const auto deviation_i = value.real() - statistics.MeanI;
        const auto deviation_q = value.imag() - statistics.MeanQ;
        statistics.VarI += deviation_i * deviation_i;
        statistics.VarQ += deviation_q * deviation_q;
        cross += deviation_i * deviation_q;
    }
    statistics.VarI /= count - 1;
    statistics.VarQ /= count - 1;
    statistics.Correlation = cross / (count - 1) / std::sqrt(statistics.VarI * statistics.VarQ);
    return statistics;
}

} // namespace

TEST(Noise, DocumentedBoxMullerFixture) {
    // Independent reimplementation of the documented conversion:
    // u = (raw + 0.5) / 2^32 strictly inside (0,1); z0 = R cos, z1 = R sin.
    std::mt19937 engine(42);
    const auto uniform = [&] { return (static_cast<double>(engine()) + 0.5) / 4294967296.0; };
    GaussianSource source(42);
    for (int pair = 0; pair < 4; ++pair) {
        const double u1 = uniform();
        const double u2 = uniform();
        EXPECT_GT(u1, 0);
        EXPECT_LT(u1, 1);
        const double radius = std::sqrt(-2 * std::log(u1));
        const double theta = 2 * std::numbers::pi * u2;
        EXPECT_DOUBLE_EQ(source.Next(), radius * std::cos(theta));
        EXPECT_DOUBLE_EQ(source.Next(), radius * std::sin(theta));
    }
}

TEST(Noise, DeterministicAndSeedSeparated) {
    const auto first = GaussianNoise(64, 1, 5490);
    const auto repeated = GaussianNoise(64, 1, 5490);
    const auto other_seed = GaussianNoise(64, 1, 5491);
    EXPECT_EQ(first, repeated);
    EXPECT_NE(first, other_seed);
    EXPECT_EQ(first.size(), 64u);
    EXPECT_THROW(GaussianNoise(4, -1, 1), std::invalid_argument);
    EXPECT_THROW(GaussianNoise(4, NAN, 1), std::invalid_argument);
    EXPECT_THROW(GaussianNoise(4, 1e100, 1), std::overflow_error);
    EXPECT_NO_THROW(GaussianNoise(0, 1, 1));
    // Zero power produces exact zeros.
    for (const auto &value : GaussianNoise(8, 0, 3)) {
        EXPECT_EQ(value, std::complex<float>(0, 0));
    }
}

TEST(Noise, WGNComponentStatistics) {
    GenerationConfig config;
    config.Modulation = Modulation::WGN;
    config.NoiseSource.SampleCount = 65536;
    config.NoiseSource.NoisePower = 4;
    const auto result = Generate(config);
    const auto stats = ComponentStatistics(result.Samples);
    // Each component carries half the total complex power.
    EXPECT_NEAR(stats.MeanI, 0, .05);
    EXPECT_NEAR(stats.MeanQ, 0, .05);
    EXPECT_NEAR(stats.VarI, 2, .1);
    EXPECT_NEAR(stats.VarQ, 2, .1);
    EXPECT_NEAR(stats.Correlation, 0, .02);
}

TEST(Noise, DisabledAwgnPreservesCleanSamples) {
    GenerationConfig clean;
    clean.SymbolCount = 64;
    GenerationConfig noisy = clean;
    noisy.Awgn.Enabled = false;
    noisy.Awgn.SnrDb = 3;
    noisy.NoiseSeed = 1234;
    EXPECT_EQ(Generate(clean).Samples, Generate(noisy).Samples);
    EXPECT_FALSE(Generate(noisy).Noise.AwgnApplied);
    // Noise settings never change the transmitted symbols.
    noisy.Awgn.Enabled = true;
    EXPECT_EQ(Generate(noisy).Symbols, Generate(clean).Symbols);
}

TEST(Noise, AwgnSnrToleranceAndReferenceInterval) {
    GenerationConfig config;
    config.SymbolCount = 512;
    config.RollOff = .35;
    config.SpanSymbols = 12;
    config.Awgn.Enabled = true;
    const auto clean = Generate([&] {
        auto clean_config = config;
        clean_config.Awgn.Enabled = false;
        return clean_config;
    }());
    for (const double snr : {0., 10., 20.}) {
        config.Awgn.SnrDb = snr;
        const auto noisy = Generate(config);
        EXPECT_TRUE(noisy.Noise.AwgnApplied);
        EXPECT_DOUBLE_EQ(noisy.Noise.RequestedSnrDb, snr);
        EXPECT_EQ(noisy.Noise.ReferenceBegin, 12u * 8u);
        EXPECT_EQ(noisy.Noise.ReferenceEnd, 512u * 8u);
        EXPECT_GT(noisy.Noise.ReferencePower, 0);
        double reference = 0;
        for (std::size_t k = noisy.Noise.ReferenceBegin; k < noisy.Noise.ReferenceEnd; ++k) {
            reference += std::norm(clean.Samples[k]);
        }
        reference /= static_cast<double>(noisy.Noise.ReferenceEnd - noisy.Noise.ReferenceBegin);
        EXPECT_NEAR(noisy.Noise.ReferencePower, reference, 1e-9);
        double noise_power = 0;
        for (std::size_t k = 0; k < noisy.Samples.size(); ++k) {
            noise_power += std::norm(noisy.Samples[k] - clean.Samples[k]);
        }
        noise_power /= static_cast<double>(noisy.Samples.size());
        const auto measured_db = 10 * std::log10(reference / noise_power);
        EXPECT_NEAR(measured_db, snr, .5);
        EXPECT_NEAR(noisy.Noise.AddedNoisePower, reference / std::pow(10., snr / 10), 1e-12);
        // The added noise is exactly the documented seeded Gaussian stream.
        const auto expected = GaussianNoise(noisy.Samples.size(), noisy.Noise.AddedNoisePower, config.NoiseSeed);
        for (std::size_t k = 0; k < noisy.Samples.size(); ++k) {
            EXPECT_FLOAT_EQ((clean.Samples[k] + expected[k]).real(), noisy.Samples[k].real());
        }
    }
    // Rectangular pulses reference the entire clean signal.
    config.Pulse = Pulse::Rectangular;
    config.Awgn.SnrDb = 10;
    const auto rect = Generate(config);
    EXPECT_EQ(rect.Noise.ReferenceBegin, 0u);
    EXPECT_EQ(rect.Noise.ReferenceEnd, rect.Samples.size());
}

TEST(Noise, AwgnSharedRealizationAcrossSnr) {
    GenerationConfig config;
    config.SymbolCount = 128;
    config.Awgn.Enabled = true;
    config.Awgn.SnrDb = 10;
    const auto low = Generate(config);
    config.Awgn.SnrDb = 20;
    const auto high = Generate(config);
    config.Awgn.Enabled = false;
    const auto clean = Generate(config);
    ASSERT_EQ(low.Samples.size(), high.Samples.size());
    // Same underlying draws: the 20 dB noise is the 10 dB noise scaled by 10^(-10/20).
    const auto ratio = std::pow(10., -10. / 20);
    for (std::size_t k = 0; k < low.Samples.size(); ++k) {
        const auto expected = clean.Samples[k] + (low.Samples[k] - clean.Samples[k]) * static_cast<float>(ratio);
        EXPECT_NEAR(high.Samples[k].real(), expected.real(), 1e-4);
        EXPECT_NEAR(high.Samples[k].imag(), expected.imag(), 1e-4);
    }
}

TEST(Noise, AwgnRejectsInvalidReferences) {
    GenerationConfig config;
    config.Awgn.Enabled = true;
    // Zero-power reference: gain zero leaves no clean energy.
    config.AmplitudeGain = 0;
    EXPECT_THROW(Generate(config), std::invalid_argument);
    // Empty reference interval: symbol count within one RRC span.
    config = {};
    config.Awgn.Enabled = true;
    config.SymbolCount = 5;
    config.SpanSymbols = 10;
    EXPECT_THROW(Generate(config), std::invalid_argument);
    // Non-finite SNR fails validation.
    config = {};
    config.Awgn.Enabled = true;
    config.Awgn.SnrDb = NAN;
    EXPECT_THROW(Validate(config), std::invalid_argument);
    config.Awgn.Enabled = false;
    EXPECT_THROW(Validate(config), std::invalid_argument);
    config = {};
    config.Modulation = Modulation::WGN;
    config.NoiseSource.SampleRateHz = 1e-305;
    EXPECT_THROW(Validate(config), std::invalid_argument);
}

TEST(Noise, IndependentDataAndNoiseStreams) {
    GenerationConfig config;
    config.SymbolCount = 64;
    config.Awgn.Enabled = true;
    const auto base = Generate(config);
    auto changed = config;
    changed.Seed++;
    EXPECT_NE(Generate(changed).Samples, base.Samples);
    changed = config;
    changed.NoiseSeed++;
    const auto other_noise = Generate(changed);
    EXPECT_NE(other_noise.Samples, base.Samples);
    // Same symbols, different noise realization.
    EXPECT_EQ(other_noise.Symbols, base.Symbols);
    EXPECT_EQ(other_noise.Noise.ReferencePower, base.Noise.ReferencePower);
}

TEST(Noise, MatchedSymbolsRejectNoiseSources) {
    GenerationConfig config;
    config.Modulation = Modulation::WGN;
    EXPECT_THROW(MatchedSymbols(Generate(config)), std::invalid_argument);
}

} // namespace Core
