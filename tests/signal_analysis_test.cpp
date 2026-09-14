#include "signal_analysis.h"
#include "plot_data.h"
#include <gtest/gtest.h>
#include <algorithm>
TEST(Analysis, MatchedTimingAndRecovery) {
    for (auto modulation : {iq::Modulation::BPSK, iq::Modulation::QPSK, iq::Modulation::PSK8}) {
        iq::GenerationConfig c;
        c.modulation = modulation; c.roll_off = .35; c.span_symbols = 12;
        const auto r = iq::generate(c);
        const auto observed = iq::matched_symbols(r);
        ASSERT_EQ(observed.values.size(), 232u);
        EXPECT_EQ(observed.symbol_indices.front(), 12u);
        for (std::size_t i = 0; i < observed.values.size(); ++i)
            EXPECT_LT(std::abs(observed.values[i] - r.symbols[observed.symbol_indices[i]]), .02);
        c.symbol_count = 1;
        EXPECT_TRUE(iq::matched_symbols(iq::generate(c)).values.empty());
        c.pulse = iq::Pulse::Rectangular;
        c.amplitude_gain = 2;
        const auto rect = iq::generate(c);
        EXPECT_EQ(iq::matched_symbols(rect).values[0], rect.symbols[0] * 2.f);
    }
}
TEST(Plots, BoundedExtremaAndPhysicalTime) {
    iq::GenerationConfig c;
    c.symbol_count = 4096;
    auto r = iq::generate(c);
    r.samples[99] = {100,-200};
    auto plot = make_plot_data(r, 256);
    EXPECT_LE(plot.time.size(), 256u);
    EXPECT_TRUE(std::is_sorted(plot.time.begin(), plot.time.end()));
    EXPECT_EQ(*std::max_element(plot.i.begin(), plot.i.end()), 100);
    EXPECT_EQ(*std::min_element(plot.q.begin(), plot.q.end()), -200);
    EXPECT_DOUBLE_EQ(plot.time.back(), (r.samples.size()-1)/r.sample_rate_hz);
    EXPECT_EQ(plot.mapped.x.size(), r.symbols.size());
}
TEST(Analysis, QAM16MatchedRecovery) {
    iq::GenerationConfig c;
    c.modulation = iq::Modulation::QAM16;
    c.span_symbols = 12; c.roll_off = .35;
    const auto r = iq::generate(c);
    const auto observed = iq::matched_symbols(r);
    ASSERT_FALSE(observed.values.empty());
    for (std::size_t i = 0; i < observed.values.size(); ++i)
        EXPECT_LT(std::abs(observed.values[i] - r.symbols[observed.symbol_indices[i]]), .03);
}
TEST(Analysis, QAM64MatchedRecoveryAndRectangularGain) {
    iq::GenerationConfig c;
    c.modulation = iq::Modulation::QAM64;
    c.span_symbols = 12;
    c.roll_off = .35;
    for (auto pulse : {iq::Pulse::RRC, iq::Pulse::Rectangular}) {
        c.pulse = pulse;
        c.amplitude_gain = 2;
        const auto r = iq::generate(c);
        const auto observed = iq::matched_symbols(r);
        ASSERT_FALSE(observed.values.empty());
        for (std::size_t k = 0; k < observed.values.size(); ++k)
            EXPECT_LT(std::abs(observed.values[k] - r.symbols[observed.symbol_indices[k]] * 2.f),
                      pulse == iq::Pulse::RRC ? .06 : 1e-6);
    }
}
#include <numbers>
#include <numeric>
TEST(Spectrum, ToneLocationSignAndIntegratedPower) {
    constexpr double fs = 8192;
    for (double frequency : {-1024.,0.,1024.}) {
        std::vector<std::complex<float>> tone(8192);
        for (std::size_t k = 0; k < tone.size(); ++k) tone[k] = std::polar(2.f, static_cast<float>(2*std::numbers::pi*frequency*k/fs));
        auto psd = iq::welch_psd(tone,fs);
        auto peak = static_cast<std::size_t>(std::max_element(psd.power_density.begin(),psd.power_density.end())-psd.power_density.begin());
        EXPECT_DOUBLE_EQ(psd.frequency_hz[peak],frequency);
        EXPECT_NEAR(std::accumulate(psd.power_density.begin(),psd.power_density.end(),0.) * fs / psd.segment_length,4.,1e-6);
        EXPECT_EQ(psd.segment_count,15u);
    }
}
TEST(Spectrum, IndependentParsevalAndInputValidation) {
    // One four-sample segment: periodic Hann {0,.5,1,.5}; weighted energy = 0+1+9+4=14.
    std::vector<std::complex<float>> data{{1,0},{2,0},{3,0},{4,0}};
    auto psd = iq::welch_psd(data,8,4);
    EXPECT_NEAR(std::accumulate(psd.power_density.begin(),psd.power_density.end(),0.) * 2,14 / 1.5,1e-12);
    EXPECT_EQ(psd.frequency_hz,(std::vector<double>{-4,-2,0,2}));
    EXPECT_TRUE(iq::welch_psd({},8).power_density.empty());
    EXPECT_THROW(iq::welch_psd(data,0),std::invalid_argument);
    EXPECT_THROW(iq::welch_psd(data,8,3),std::invalid_argument);
    EXPECT_THROW(iq::welch_psd({{NAN,0}},8),std::invalid_argument);
}
