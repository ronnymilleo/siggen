#include "generator.h"
#include "noise.h"
#include "signal_analysis.h"
#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
#include <random>

using namespace iq;

namespace {
struct Statistics {
    double mean_i = 0, mean_q = 0, var_i = 0, var_q = 0, correlation = 0;
};
Statistics component_statistics(const std::vector<std::complex<float>>& samples) {
    Statistics s;
    const auto n = static_cast<double>(samples.size());
    for (const auto& value : samples) {
        s.mean_i += value.real();
        s.mean_q += value.imag();
    }
    s.mean_i /= n;
    s.mean_q /= n;
    double cross = 0;
    for (const auto& value : samples) {
        const auto di = value.real() - s.mean_i;
        const auto dq = value.imag() - s.mean_q;
        s.var_i += di * di;
        s.var_q += dq * dq;
        cross += di * dq;
    }
    s.var_i /= n - 1;
    s.var_q /= n - 1;
    s.correlation = cross / (n - 1) / std::sqrt(s.var_i * s.var_q);
    return s;
}
}

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
        EXPECT_DOUBLE_EQ(source.next(), radius * std::cos(theta));
        EXPECT_DOUBLE_EQ(source.next(), radius * std::sin(theta));
    }
}

TEST(Noise, DeterministicAndSeedSeparated) {
    const auto a = gaussian_noise(64, 1, 5490);
    const auto b = gaussian_noise(64, 1, 5490);
    const auto c = gaussian_noise(64, 1, 5491);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_EQ(a.size(), 64u);
    EXPECT_THROW(gaussian_noise(4, -1, 1), std::invalid_argument);
    EXPECT_THROW(gaussian_noise(4, NAN, 1), std::invalid_argument);
    EXPECT_THROW(gaussian_noise(4, 1e100, 1), std::overflow_error);
    EXPECT_NO_THROW(gaussian_noise(0, 1, 1));
    // Zero power produces exact zeros.
    for (const auto& value : gaussian_noise(8, 0, 3)) EXPECT_EQ(value, std::complex<float>(0, 0));
}

TEST(Noise, WGNComponentStatistics) {
    GenerationConfig config;
    config.modulation = Modulation::WGN;
    config.noise_source.sample_count = 65536;
    config.noise_source.noise_power = 4;
    const auto result = generate(config);
    const auto stats = component_statistics(result.samples);
    // Each component carries half the total complex power.
    EXPECT_NEAR(stats.mean_i, 0, .05);
    EXPECT_NEAR(stats.mean_q, 0, .05);
    EXPECT_NEAR(stats.var_i, 2, .1);
    EXPECT_NEAR(stats.var_q, 2, .1);
    EXPECT_NEAR(stats.correlation, 0, .02);
}

TEST(Noise, DisabledAwgnPreservesCleanSamples) {
    GenerationConfig clean;
    clean.symbol_count = 64;
    GenerationConfig noisy = clean;
    noisy.awgn.enabled = false;
    noisy.awgn.snr_db = 3;
    noisy.noise_seed = 1234;
    EXPECT_EQ(generate(clean).samples, generate(noisy).samples);
    EXPECT_FALSE(generate(noisy).noise.awgn_applied);
    // Noise settings never change the transmitted symbols.
    noisy.awgn.enabled = true;
    EXPECT_EQ(generate(noisy).symbols, generate(clean).symbols);
}

TEST(Noise, AwgnSnrToleranceAndReferenceInterval) {
    GenerationConfig config;
    config.symbol_count = 512;
    config.roll_off = .35;
    config.span_symbols = 12;
    config.awgn.enabled = true;
    const auto clean = generate([&] { auto c = config; c.awgn.enabled = false; return c; }());
    for (const double snr : {0., 10., 20.}) {
        config.awgn.snr_db = snr;
        const auto noisy = generate(config);
        EXPECT_TRUE(noisy.noise.awgn_applied);
        EXPECT_DOUBLE_EQ(noisy.noise.requested_snr_db, snr);
        EXPECT_EQ(noisy.noise.reference_begin, 12u * 8u);
        EXPECT_EQ(noisy.noise.reference_end, 512u * 8u);
        EXPECT_GT(noisy.noise.reference_power, 0);
        double reference = 0;
        for (std::size_t k = noisy.noise.reference_begin; k < noisy.noise.reference_end; ++k)
            reference += std::norm(clean.samples[k]);
        reference /= static_cast<double>(noisy.noise.reference_end - noisy.noise.reference_begin);
        EXPECT_NEAR(noisy.noise.reference_power, reference, 1e-9);
        double noise_power = 0;
        for (std::size_t k = 0; k < noisy.samples.size(); ++k)
            noise_power += std::norm(noisy.samples[k] - clean.samples[k]);
        noise_power /= static_cast<double>(noisy.samples.size());
        const auto measured_db = 10 * std::log10(reference / noise_power);
        EXPECT_NEAR(measured_db, snr, .5);
        EXPECT_NEAR(noisy.noise.added_noise_power, reference / std::pow(10., snr / 10), 1e-12);
        // The added noise is exactly the documented seeded Gaussian stream.
        const auto expected = gaussian_noise(noisy.samples.size(), noisy.noise.added_noise_power, config.noise_seed);
        for (std::size_t k = 0; k < noisy.samples.size(); ++k)
            EXPECT_FLOAT_EQ((clean.samples[k] + expected[k]).real(), noisy.samples[k].real());
    }
    // Rectangular pulses reference the entire clean signal.
    config.pulse = Pulse::Rectangular;
    config.awgn.snr_db = 10;
    const auto rect = generate(config);
    EXPECT_EQ(rect.noise.reference_begin, 0u);
    EXPECT_EQ(rect.noise.reference_end, rect.samples.size());
}

TEST(Noise, AwgnSharedRealizationAcrossSnr) {
    GenerationConfig config;
    config.symbol_count = 128;
    config.awgn.enabled = true;
    config.awgn.snr_db = 10;
    const auto low = generate(config);
    config.awgn.snr_db = 20;
    const auto high = generate(config);
    config.awgn.enabled = false;
    const auto clean = generate(config);
    ASSERT_EQ(low.samples.size(), high.samples.size());
    // Same underlying draws: the 20 dB noise is the 10 dB noise scaled by 10^(-10/20).
    const auto ratio = std::pow(10., -10. / 20);
    for (std::size_t k = 0; k < low.samples.size(); ++k) {
        const auto expected = clean.samples[k] + (low.samples[k] - clean.samples[k]) * static_cast<float>(ratio);
        EXPECT_NEAR(high.samples[k].real(), expected.real(), 1e-4);
        EXPECT_NEAR(high.samples[k].imag(), expected.imag(), 1e-4);
    }
}

TEST(Noise, AwgnRejectsInvalidReferences) {
    GenerationConfig config;
    config.awgn.enabled = true;
    // Zero-power reference: gain zero leaves no clean energy.
    config.amplitude_gain = 0;
    EXPECT_THROW(generate(config), std::invalid_argument);
    // Empty reference interval: symbol count within one RRC span.
    config = {};
    config.awgn.enabled = true;
    config.symbol_count = 5;
    config.span_symbols = 10;
    EXPECT_THROW(generate(config), std::invalid_argument);
    // Non-finite SNR fails validation.
    config = {};
    config.awgn.enabled = true;
    config.awgn.snr_db = NAN;
    EXPECT_THROW(validate(config), std::invalid_argument);
    config.awgn.enabled = false;
    EXPECT_THROW(validate(config), std::invalid_argument);
    config = {};
    config.modulation = Modulation::WGN;
    config.noise_source.sample_rate_hz = 1e-305;
    EXPECT_THROW(validate(config), std::invalid_argument);
}

TEST(Noise, IndependentDataAndNoiseStreams) {
    GenerationConfig config;
    config.symbol_count = 64;
    config.awgn.enabled = true;
    const auto base = generate(config);
    auto changed = config;
    changed.seed++;
    EXPECT_NE(generate(changed).samples, base.samples);
    changed = config;
    changed.noise_seed++;
    const auto other_noise = generate(changed);
    EXPECT_NE(other_noise.samples, base.samples);
    // Same symbols, different noise realization.
    EXPECT_EQ(other_noise.symbols, base.symbols);
    EXPECT_EQ(other_noise.noise.reference_power, base.noise.reference_power);
}

TEST(Noise, MatchedSymbolsRejectNoiseSources) {
    GenerationConfig config;
    config.modulation = Modulation::WGN;
    EXPECT_THROW(matched_symbols(generate(config)), std::invalid_argument);
}
