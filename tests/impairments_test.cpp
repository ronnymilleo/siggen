/**
 * @file    impairments_test.cpp
 * @brief   Tests for receiver impairments: CFO, phase noise, IQ imbalance, DC offset and ADC quantization.
 */

#include "impairments.h"

#include "batch.h"
#include "generator.h"
#include "preset.h"
#include <cmath>
#include <gtest/gtest.h>
#include <numbers>
#include <set>

namespace Core {

namespace {

GenerationConfig Qpsk() {
    GenerationConfig config;
    config.Modulation = Modulation::QPSK;
    config.SymbolCount = 128;
    config.Pulse = Pulse::Rectangular;
    config.SamplesPerSymbol = 4;
    return config;
}

std::vector<std::complex<float>> Constant(std::size_t count) {
    return std::vector<std::complex<float>>(count, {1.f, 0.f});
}

} // namespace

TEST(Impairments, DefaultsAreInactiveAndLeaveOutputBitIdentical) {
    const auto clean = Generate(Qpsk());
    auto config = Qpsk();
    config.Impairments = {};
    config.ImpairmentSeed = 1;
    const auto again = Generate(config);
    EXPECT_FALSE(clean.ImpairmentsApplied);
    EXPECT_EQ(clean.Samples, again.Samples);
}

TEST(Impairments, CfoRotatesAtTheRequestedRate) {
    auto samples = Constant(1000);
    ImpairmentSettings settings;
    settings.CfoHz = 100;
    ApplyImpairments(samples, 1000, settings, 1);
    for (std::size_t n = 0; n < samples.size(); n += 37) {
        const auto angle = 2 * std::numbers::pi * 100 * static_cast<double>(n) / 1000;
        EXPECT_NEAR(samples[n].real(), std::cos(angle), 2e-5);
        EXPECT_NEAR(samples[n].imag(), std::sin(angle), 2e-5);
    }
}

TEST(Impairments, PhaseNoiseKeepsMagnitudeAndRandomWalksWithExpectedVariance) {
    const std::size_t sample_count = 20000;
    auto samples = Constant(sample_count);
    ImpairmentSettings settings;
    settings.PhaseNoiseLinewidthHz = 10;
    ApplyImpairments(samples, 1000, settings, 7);
    double step_variance = 0;
    for (std::size_t k = 1; k < sample_count; ++k) {
        EXPECT_NEAR(std::abs(samples[k]), 1., 1e-5);
        const auto step = std::arg(samples[k] * std::conj(samples[k - 1]));
        step_variance += step * step;
    }
    step_variance /= static_cast<double>(sample_count - 1);
    EXPECT_NEAR(step_variance, 2 * std::numbers::pi * 10 / 1000, 0.1 * 2 * std::numbers::pi * 10 / 1000);
    auto again = Constant(sample_count);
    ApplyImpairments(again, 1000, settings, 7);
    EXPECT_EQ(samples, again);
    auto other = Constant(sample_count);
    ApplyImpairments(other, 1000, settings, 8);
    EXPECT_NE(samples, other);
}

TEST(Impairments, IqImbalanceScalesAndSkewsQ) {
    std::vector<std::complex<float>> samples{{1.f, 1.f}, {-1.f, 2.f}};
    ImpairmentSettings settings;
    settings.IqGainDb = 6.0205999;
    ApplyImpairments(samples, 1, settings, 0);
    EXPECT_NEAR(samples[0].imag(), 2.f, 1e-4);
    EXPECT_NEAR(samples[0].real(), 1.f, 1e-6);
    std::vector<std::complex<float>> skewed{{1.f, 0.f}};
    ImpairmentSettings skew;
    skew.IqPhaseDeg = 30;
    ApplyImpairments(skewed, 1, skew, 0);
    EXPECT_NEAR(skewed[0].imag(), 0.5f, 1e-6);
}

TEST(Impairments, DcOffsetIsRelativeToRms) {
    std::vector<std::complex<float>> samples{{3.f, 4.f}, {3.f, 4.f}}; // RMS amplitude 5.
    ImpairmentSettings settings;
    settings.DcOffsetI = 0.2;
    settings.DcOffsetQ = -0.1;
    ApplyImpairments(samples, 1, settings, 0);
    EXPECT_NEAR(samples[0].real(), 4.f, 1e-6);
    EXPECT_NEAR(samples[0].imag(), 3.5f, 1e-6);
}

TEST(Impairments, QuantizerUsesAtMostTwoToTheBitsLevels) {
    auto config = Qpsk();
    config.Pulse = Pulse::RRC;
    config.SymbolCount = 256;
    config.Impairments.AdcBits = 3;
    const auto result = Generate(config);
    ASSERT_TRUE(result.ImpairmentsApplied);
    std::set<float> levels;
    for (const auto &x : result.Samples) {
        levels.insert(x.real());
        levels.insert(x.imag());
    }
    EXPECT_LE(levels.size(), 8u);
    EXPECT_GT(levels.size(), 2u);
    auto clean = Qpsk();
    clean.Pulse = Pulse::RRC;
    clean.SymbolCount = 256;
    const auto reference = Generate(clean);
    double peak = 0;
    for (const auto &x : reference.Samples) {
        peak = std::max({peak, static_cast<double>(std::abs(x.real())), static_cast<double>(std::abs(x.imag()))});
    }
    const double step = 2 * peak / 8;
    for (std::size_t k = 0; k < reference.Samples.size(); ++k) {
        EXPECT_LE(std::abs(result.Samples[k].real() - reference.Samples[k].real()), step);
    }
}

TEST(Impairments, ValidationRejectsBadSettingsAndNoiseSources) {
    ImpairmentSettings settings;
    settings.AdcBits = 1;
    EXPECT_THROW(Validate(settings), std::invalid_argument);
    settings = {};
    settings.PhaseNoiseLinewidthHz = -1;
    EXPECT_THROW(Validate(settings), std::invalid_argument);
    settings = {};
    settings.CfoHz = std::nan("");
    EXPECT_THROW(Validate(settings), std::invalid_argument);
    GenerationConfig wgn;
    wgn.Modulation = Modulation::WGN;
    wgn.Impairments.CfoHz = 5;
    EXPECT_THROW(Validate(wgn), std::invalid_argument);
}

TEST(Impairments, PresetRoundTripAndVersioning) {
    auto config = Qpsk();
    EXPECT_NE(SerializePreset(config).find("Version=2"), std::string::npos);
    config.Impairments.CfoHz = -12.5;
    config.Impairments.PhaseNoiseLinewidthHz = 3;
    config.Impairments.IqGainDb = 1.25;
    config.Impairments.IqPhaseDeg = -4;
    config.Impairments.DcOffsetI = .1;
    config.Impairments.DcOffsetQ = -.2;
    config.Impairments.AdcBits = 6;
    config.ImpairmentSeed = 99;
    const auto text = SerializePreset(config);
    EXPECT_NE(text.find("Version=3"), std::string::npos);
    EXPECT_EQ(ParsePreset(text), config);
    EXPECT_THROW(ParsePreset(text.substr(0, text.find("CfoHz"))), std::invalid_argument);
    auto v2 = SerializePreset(Qpsk());
    EXPECT_THROW(ParsePreset(v2 + "CfoHz=1\n"), std::invalid_argument);
}

TEST(Impairments, BatchFramesApplyImpairmentsAfterCropWithDerivedSeed) {
    auto base = Qpsk();
    base.Impairments.PhaseNoiseLinewidthHz = 20;
    base.Impairments.CfoHz = 15;
    const auto frame = GenerateFrame(base, 256, 3, std::nullopt);
    ASSERT_TRUE(frame.ImpairmentsApplied);
    EXPECT_EQ(frame.ImpairmentSeed, DeriveImpairmentSeed(base.Seed, base.Modulation, 3, base.ImpairmentSeed));
    auto clean_base = base;
    clean_base.Impairments = {};
    const auto clean = GenerateFrame(clean_base, 256, 3, std::nullopt);
    EXPECT_FALSE(clean.ImpairmentsApplied);
    auto expected = clean.Samples;
    ApplyImpairments(expected, clean.SampleRateHz, base.Impairments, frame.ImpairmentSeed);
    EXPECT_EQ(frame.Samples, expected);
    EXPECT_NE(frame.ImpairmentSeed, DeriveImpairmentSeed(base.Seed, base.Modulation, 4, base.ImpairmentSeed));
}

} // namespace Core
