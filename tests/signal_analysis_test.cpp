/**
 * @file    signal_analysis_test.cpp
 * @brief   Tests for matched-filter symbol recovery, plot decimation and the Welch power spectral density.
 */

#include "signal_analysis.h"

#include "plot_data.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <numbers>
#include <numeric>

namespace Core {

TEST(Analysis, MatchedTimingAndRecovery) {
    for (auto modulation : {Modulation::BPSK, Modulation::QPSK, Modulation::PSK8}) {
        GenerationConfig config;
        config.Modulation = modulation;
        config.RollOff = .35;
        config.SpanSymbols = 12;
        const auto result = Generate(config);
        const auto observed = MatchedSymbols(result);
        ASSERT_EQ(observed.Values.size(), 232u);
        EXPECT_EQ(observed.SymbolIndices.front(), 12u);
        for (std::size_t i = 0; i < observed.Values.size(); ++i) {
            EXPECT_LT(std::abs(observed.Values[i] - result.Symbols[observed.SymbolIndices[i]]), .02);
        }
        config.SymbolCount = 1;
        EXPECT_TRUE(MatchedSymbols(Generate(config)).Values.empty());
        config.Pulse = Pulse::Rectangular;
        config.AmplitudeGain = 2;
        const auto rect = Generate(config);
        EXPECT_EQ(MatchedSymbols(rect).Values[0], rect.Symbols[0] * 2.f);
    }
}

TEST(Plots, BoundedExtremaAndPhysicalTime) {
    GenerationConfig config;
    config.SymbolCount = 4096;
    auto result = Generate(config);
    result.Samples[99] = {100, -200};
    auto plot = MakePlotData(result, 256);
    EXPECT_LE(plot.Time.size(), 256u);
    EXPECT_TRUE(std::is_sorted(plot.Time.begin(), plot.Time.end()));
    EXPECT_EQ(*std::max_element(plot.I.begin(), plot.I.end()), 100);
    EXPECT_EQ(*std::min_element(plot.Q.begin(), plot.Q.end()), -200);
    EXPECT_DOUBLE_EQ(plot.Time.back(), (result.Samples.size() - 1) / result.SampleRateHz);
    EXPECT_EQ(plot.Mapped.X.size(), result.Symbols.size());
}

TEST(Analysis, QAM16MatchedRecovery) {
    GenerationConfig config;
    config.Modulation = Modulation::QAM16;
    config.SpanSymbols = 12;
    config.RollOff = .35;
    const auto result = Generate(config);
    const auto observed = MatchedSymbols(result);
    ASSERT_FALSE(observed.Values.empty());
    for (std::size_t i = 0; i < observed.Values.size(); ++i) {
        EXPECT_LT(std::abs(observed.Values[i] - result.Symbols[observed.SymbolIndices[i]]), .03);
    }
}

TEST(Analysis, QAM64MatchedRecoveryAndRectangularGain) {
    GenerationConfig config;
    config.Modulation = Modulation::QAM64;
    config.SpanSymbols = 12;
    config.RollOff = .35;
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular}) {
        config.Pulse = pulse;
        config.AmplitudeGain = 2;
        const auto result = Generate(config);
        const auto observed = MatchedSymbols(result);
        ASSERT_FALSE(observed.Values.empty());
        for (std::size_t k = 0; k < observed.Values.size(); ++k) {
            EXPECT_LT(std::abs(observed.Values[k] - result.Symbols[observed.SymbolIndices[k]] * 2.f),
                      pulse == Pulse::RRC ? .06 : 1e-6);
        }
    }
}

TEST(Spectrum, ToneLocationSignAndIntegratedPower) {
    constexpr double Fs = 8192;
    for (double frequency : {-1024., 0., 1024.}) {
        std::vector<std::complex<float>> tone(8192);
        for (std::size_t k = 0; k < tone.size(); ++k) {
            tone[k] = std::polar(2.f, static_cast<float>(2 * std::numbers::pi * frequency * k / Fs));
        }
        auto psd = WelchPsd(tone, Fs);
        auto peak = static_cast<std::size_t>(std::max_element(psd.PowerDensity.begin(), psd.PowerDensity.end()) -
                                             psd.PowerDensity.begin());
        EXPECT_DOUBLE_EQ(psd.FrequencyHz[peak], frequency);
        EXPECT_NEAR(std::accumulate(psd.PowerDensity.begin(), psd.PowerDensity.end(), 0.) * Fs / psd.SegmentLength, 4.,
                    1e-6);
        EXPECT_EQ(psd.SegmentCount, 15u);
    }
}

TEST(Spectrum, IndependentParsevalAndInputValidation) {
    // One four-sample segment: periodic Hann {0,.5,1,.5}; weighted energy = 0+1+9+4=14.
    std::vector<std::complex<float>> data{{1, 0}, {2, 0}, {3, 0}, {4, 0}};
    auto psd = WelchPsd(data, 8, 4);
    EXPECT_NEAR(std::accumulate(psd.PowerDensity.begin(), psd.PowerDensity.end(), 0.) * 2, 14 / 1.5, 1e-12);
    EXPECT_EQ(psd.FrequencyHz, (std::vector<double>{-4, -2, 0, 2}));
    EXPECT_TRUE(WelchPsd({}, 8).PowerDensity.empty());
    EXPECT_THROW(WelchPsd(data, 0), std::invalid_argument);
    EXPECT_THROW(WelchPsd(data, 8, 3), std::invalid_argument);
    EXPECT_THROW(WelchPsd({{NAN, 0}}, 8), std::invalid_argument);
}

} // namespace Core
