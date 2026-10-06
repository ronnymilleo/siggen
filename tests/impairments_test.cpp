#include "batch.h"
#include "generator.h"
#include "impairments.h"
#include "preset.h"
#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
#include <set>

using namespace iq;

namespace {
GenerationConfig qpsk() {
    GenerationConfig c;
    c.modulation = Modulation::QPSK;
    c.symbol_count = 128;
    c.pulse = Pulse::Rectangular;
    c.samples_per_symbol = 4;
    return c;
}
std::vector<std::complex<float>> constant(std::size_t n) { return std::vector<std::complex<float>>(n, {1.f, 0.f}); }
}

TEST(Impairments, DefaultsAreInactiveAndLeaveOutputBitIdentical) {
    const auto clean = generate(qpsk());
    auto config = qpsk();
    config.impairments = {};
    config.impairment_seed = 1;
    const auto again = generate(config);
    EXPECT_FALSE(clean.impairments_applied);
    EXPECT_EQ(clean.samples, again.samples);
}

TEST(Impairments, CfoRotatesAtTheRequestedRate) {
    auto x = constant(1000);
    ImpairmentSettings s;
    s.cfo_hz = 100;
    apply_impairments(x, 1000, s, 1);
    for (std::size_t n = 0; n < x.size(); n += 37) {
        const auto angle = 2 * std::numbers::pi * 100 * static_cast<double>(n) / 1000;
        EXPECT_NEAR(x[n].real(), std::cos(angle), 2e-5);
        EXPECT_NEAR(x[n].imag(), std::sin(angle), 2e-5);
    }
}

TEST(Impairments, PhaseNoiseKeepsMagnitudeAndRandomWalksWithExpectedVariance) {
    const std::size_t n = 20000;
    auto x = constant(n);
    ImpairmentSettings s;
    s.phase_noise_linewidth_hz = 10;
    apply_impairments(x, 1000, s, 7);
    double step_variance = 0;
    for (std::size_t k = 1; k < n; ++k) {
        EXPECT_NEAR(std::abs(x[k]), 1., 1e-5);
        const auto step = std::arg(x[k] * std::conj(x[k - 1]));
        step_variance += step * step;
    }
    step_variance /= static_cast<double>(n - 1);
    EXPECT_NEAR(step_variance, 2 * std::numbers::pi * 10 / 1000, 0.1 * 2 * std::numbers::pi * 10 / 1000);
    auto again = constant(n);
    apply_impairments(again, 1000, s, 7);
    EXPECT_EQ(x, again);
    auto other = constant(n);
    apply_impairments(other, 1000, s, 8);
    EXPECT_NE(x, other);
}

TEST(Impairments, IqImbalanceScalesAndSkewsQ) {
    std::vector<std::complex<float>> x{{1.f, 1.f}, {-1.f, 2.f}};
    ImpairmentSettings s;
    s.iq_gain_db = 6.0205999;
    apply_impairments(x, 1, s, 0);
    EXPECT_NEAR(x[0].imag(), 2.f, 1e-4);
    EXPECT_NEAR(x[0].real(), 1.f, 1e-6);
    std::vector<std::complex<float>> y{{1.f, 0.f}};
    ImpairmentSettings skew;
    skew.iq_phase_deg = 30;
    apply_impairments(y, 1, skew, 0);
    EXPECT_NEAR(y[0].imag(), 0.5f, 1e-6);
}

TEST(Impairments, DcOffsetIsRelativeToRms) {
    std::vector<std::complex<float>> x{{3.f, 4.f}, {3.f, 4.f}}; // RMS amplitude 5.
    ImpairmentSettings s;
    s.dc_offset_i = 0.2;
    s.dc_offset_q = -0.1;
    apply_impairments(x, 1, s, 0);
    EXPECT_NEAR(x[0].real(), 4.f, 1e-6);
    EXPECT_NEAR(x[0].imag(), 3.5f, 1e-6);
}

TEST(Impairments, QuantizerUsesAtMostTwoToTheBitsLevels) {
    auto config = qpsk();
    config.pulse = Pulse::RRC;
    config.symbol_count = 256;
    config.impairments.adc_bits = 3;
    const auto result = generate(config);
    ASSERT_TRUE(result.impairments_applied);
    std::set<float> levels;
    for (const auto& x : result.samples) { levels.insert(x.real()); levels.insert(x.imag()); }
    EXPECT_LE(levels.size(), 8u);
    EXPECT_GT(levels.size(), 2u);
    auto clean = qpsk();
    clean.pulse = Pulse::RRC;
    clean.symbol_count = 256;
    const auto reference = generate(clean);
    double peak = 0;
    for (const auto& x : reference.samples) peak = std::max({peak, (double)std::abs(x.real()), (double)std::abs(x.imag())});
    const double step = 2 * peak / 8;
    for (std::size_t k = 0; k < reference.samples.size(); ++k)
        EXPECT_LE(std::abs(result.samples[k].real() - reference.samples[k].real()), step);
}

TEST(Impairments, ValidationRejectsBadSettingsAndNoiseSources) {
    ImpairmentSettings s;
    s.adc_bits = 1;
    EXPECT_THROW(validate(s), std::invalid_argument);
    s = {};
    s.phase_noise_linewidth_hz = -1;
    EXPECT_THROW(validate(s), std::invalid_argument);
    s = {};
    s.cfo_hz = std::nan("");
    EXPECT_THROW(validate(s), std::invalid_argument);
    GenerationConfig wgn;
    wgn.modulation = Modulation::WGN;
    wgn.impairments.cfo_hz = 5;
    EXPECT_THROW(validate(wgn), std::invalid_argument);
}

TEST(Impairments, PresetRoundTripAndVersioning) {
    auto c = qpsk();
    EXPECT_NE(serialize_preset(c).find("Version=2"), std::string::npos);
    c.impairments.cfo_hz = -12.5;
    c.impairments.phase_noise_linewidth_hz = 3;
    c.impairments.iq_gain_db = 1.25;
    c.impairments.iq_phase_deg = -4;
    c.impairments.dc_offset_i = .1;
    c.impairments.dc_offset_q = -.2;
    c.impairments.adc_bits = 6;
    c.impairment_seed = 99;
    const auto text = serialize_preset(c);
    EXPECT_NE(text.find("Version=3"), std::string::npos);
    EXPECT_EQ(parse_preset(text), c);
    EXPECT_THROW(parse_preset(text.substr(0, text.find("CfoHz"))), std::invalid_argument);
    auto v2 = serialize_preset(qpsk());
    EXPECT_THROW(parse_preset(v2 + "CfoHz=1\n"), std::invalid_argument);
}

TEST(Impairments, BatchFramesApplyImpairmentsAfterCropWithDerivedSeed) {
    auto base = qpsk();
    base.impairments.phase_noise_linewidth_hz = 20;
    base.impairments.cfo_hz = 15;
    const auto frame = generate_frame(base, 256, 3, std::nullopt);
    ASSERT_TRUE(frame.impairments_applied);
    EXPECT_EQ(frame.impairment_seed, derive_impairment_seed(base.seed, base.modulation, 3, base.impairment_seed));
    auto clean_base = base;
    clean_base.impairments = {};
    const auto clean = generate_frame(clean_base, 256, 3, std::nullopt);
    EXPECT_FALSE(clean.impairments_applied);
    auto expected = clean.samples;
    apply_impairments(expected, clean.sample_rate_hz, base.impairments, frame.impairment_seed);
    EXPECT_EQ(frame.samples, expected);
    EXPECT_NE(frame.impairment_seed, derive_impairment_seed(base.seed, base.modulation, 4, base.impairment_seed));
}
