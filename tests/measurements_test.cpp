/**
 * @file    measurements_test.cpp
 * @brief   Tests for power statistics, EVM and SNR measurement, eye diagrams, spectral windows and guided presets.
 */

#include "measurements.h"

#include "preset.h"
#include "signal_analysis.h"
#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>
#include <numbers>

namespace Core {

TEST(Measurements, PowerStatisticsAndPapr) {
    EXPECT_EQ(MeasurePowerStatistics({}).MeanPower, 0);
    const auto zero = MeasurePowerStatistics(std::vector<std::complex<float>>(4));
    EXPECT_EQ(zero.PaprDb, 0);
    const auto stats = MeasurePowerStatistics({{1, 0}, {0, 1}, {-1, 0}, {0, 2}});
    EXPECT_DOUBLE_EQ(stats.MeanPower, 7. / 4);
    EXPECT_DOUBLE_EQ(stats.PeakPower, 4);
    EXPECT_NEAR(stats.PaprDb, 10 * std::log10(4 / (7. / 4)), 1e-12);
    EXPECT_THROW(MeasurePowerStatistics({{std::nanf(""), 0}}), std::invalid_argument);
    // Rectangular BPSK has a constant envelope.
    GenerationConfig config;
    config.Pulse = Pulse::Rectangular;
    EXPECT_NEAR(MeasurePowerStatistics(Generate(config).Samples).PaprDb, 0, 1e-6);
    // Shaped higher-order QAM has a clearly higher PAPR than shaped BPSK.
    GenerationConfig qam;
    qam.Modulation = Modulation::QAM64;
    qam.SymbolCount = 1024;
    GenerationConfig bpsk;
    bpsk.SymbolCount = 1024;
    EXPECT_GT(MeasurePowerStatistics(Generate(qam).Samples).PaprDb,
              MeasurePowerStatistics(Generate(bpsk).Samples).PaprDb);
}

TEST(Measurements, CleanSignalHasNearZeroEvm) {
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular}) {
        GenerationConfig config;
        config.Modulation = Modulation::QAM16;
        config.Pulse = pulse;
        config.AmplitudeGain = 3;
        const auto accuracy = MeasureSymbolAccuracy(Generate(config));
        ASSERT_TRUE(accuracy);
        // RRC truncated to 10 symbols at roll-off 0.2 leaves a small residual ISI floor (~-43 dB).
        EXPECT_LT(accuracy->EvmRms, pulse == Pulse::RRC ? 1.5e-2 : 1e-3);
        EXPECT_GT(accuracy->SnrAfterMatchedDb, pulse == Pulse::RRC ? 36 : 55);
        EXPECT_NEAR(accuracy->ExpectedOffsetDb, 10 * std::log10(8.), 1e-12);
    }
}

TEST(Measurements, MeasuredSnrTracksRequestedSnr) {
    // Post-matched-filter SNR equals sample SNR + 10 log10(SPS) in expectation.
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular}) {
        for (double snr : {0., 8., 16.}) {
            GenerationConfig config;
            config.Modulation = Modulation::QPSK;
            config.Pulse = pulse;
            config.SymbolCount = 8192;
            config.Awgn.Enabled = true;
            config.Awgn.SnrDb = snr;
            const auto accuracy = MeasureSymbolAccuracy(Generate(config));
            ASSERT_TRUE(accuracy);
            EXPECT_NEAR(accuracy->SnrAfterMatchedDb, snr + accuracy->ExpectedOffsetDb, 0.6) << "snr " << snr;
            EXPECT_NEAR(accuracy->EvmDb, -accuracy->SnrAfterMatchedDb, 1e-12);
        }
    }
}

TEST(Measurements, UnavailableWithoutSymbols) {
    GenerationConfig wgn;
    wgn.Modulation = Modulation::WGN;
    EXPECT_FALSE(MeasureSymbolAccuracy(Generate(wgn)));
    EXPECT_THROW(BuildEyeDiagram(Generate(wgn)), std::invalid_argument);
    GenerationConfig tiny;
    tiny.SymbolCount = 1;
    EXPECT_FALSE(MeasureSymbolAccuracy(Generate(tiny)));
    EXPECT_TRUE(BuildEyeDiagram(Generate(tiny)).InPhase.empty());
    EXPECT_THROW(BuildEyeDiagram(Generate({}), 0), std::invalid_argument);
}

TEST(Eye, TracesCrossTheDecisionInstantAtTheSymbolValue) {
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular}) {
        GenerationConfig config;
        config.Modulation = Modulation::BPSK;
        config.Pulse = pulse;
        config.SymbolCount = 128;
        const auto result = Generate(config);
        const auto eye = BuildEyeDiagram(result, 50);
        ASSERT_EQ(eye.TimeSymbols.size(), 17u);
        EXPECT_DOUBLE_EQ(eye.TimeSymbols.front(), -1);
        EXPECT_DOUBLE_EQ(eye.TimeSymbols[8], 0);
        EXPECT_DOUBLE_EQ(eye.TimeSymbols.back(), 1);
        ASSERT_EQ(eye.InPhase.size(), 50u);
        ASSERT_EQ(eye.Quadrature.size(), 50u);
        for (std::size_t t = 0; t < eye.InPhase.size(); ++t) {
            ASSERT_EQ(eye.InPhase[t].size(), 17u);
            // Matched-filter output at the centre is one BPSK symbol: +/-1, Q zero.
            const auto tolerance = pulse == Pulse::RRC ? 2e-2 : 1e-3; // RRC truncation ISI.
            EXPECT_NEAR(std::abs(eye.InPhase[t][8]), 1, tolerance);
            EXPECT_NEAR(eye.Quadrature[t][8], 0, tolerance);
        }
    }
    GenerationConfig config;
    config.SymbolCount = 20;
    EXPECT_EQ(BuildEyeDiagram(Generate(config), 1000).InPhase.size(), 0u); // 20 symbols < 2 * span.
    config.SymbolCount = 64;
    EXPECT_EQ(BuildEyeDiagram(Generate(config), 1000).InPhase.size(), 44u);
}

TEST(Windows, ValuesAndPsdDefaults) {
    const auto window_length = std::size_t{8};
    EXPECT_DOUBLE_EQ(WindowValue(Window::Hann, 0, window_length), 0);
    EXPECT_DOUBLE_EQ(WindowValue(Window::Hann, 4, window_length), 1);
    EXPECT_NEAR(WindowValue(Window::Hamming, 0, window_length), .08, 1e-12);
    EXPECT_NEAR(WindowValue(Window::Blackman, 0, window_length), 0, 1e-12);
    EXPECT_NEAR(WindowValue(Window::Blackman, 4, window_length), 1, 1e-12);
    EXPECT_EQ(WindowValue(Window::Rectangular, 3, window_length), 1);
    // The default window remains periodic Hann.
    GenerationConfig config;
    const auto result = Generate(config);
    const auto implicit = WelchPsd(result.Samples, result.SampleRateHz);
    const auto hann = WelchPsd(result.Samples, result.SampleRateHz, 1024, Window::Hann);
    EXPECT_EQ(implicit.PowerDensity, hann.PowerDensity);
    EXPECT_EQ(hann.Window, Window::Hann);
    const auto rect = WelchPsd(result.Samples, result.SampleRateHz, 1024, Window::Rectangular);
    EXPECT_NE(rect.PowerDensity, hann.PowerDensity);
    EXPECT_EQ(rect.Window, Window::Rectangular);
    // Power is preserved across windows: integrated PSD is within a few percent.
    const auto total = [](const Spectrum &spectrum) {
        double sum = 0;
        for (auto density : spectrum.PowerDensity) {
            sum += density;
        }
        return sum;
    };
    for (auto window : {Window::Hamming, Window::Blackman}) {
        EXPECT_NEAR(total(WelchPsd(result.Samples, result.SampleRateHz, 1024, window)) / total(hann), 1, .15);
    }
}

TEST(Windows, BlackmanHasLowerSidelobesThanRectangular) {
    // A single off-bin tone leaks widely with a rectangular window.
    std::vector<std::complex<float>> tone(4096);
    for (std::size_t k = 0; k < tone.size(); ++k) {
        tone[k] = std::polar(1.f, static_cast<float>(2 * std::numbers::pi * 100.37 * k / 1024));
    }
    const auto far_leakage = [&](Window window) {
        const auto spectrum = WelchPsd(tone, 1024, 1024, window);
        double near = 0, far = 0;
        for (std::size_t k = 0; k < spectrum.PowerDensity.size(); ++k) {
            (std::abs(static_cast<int>(k) - (512 + 100)) <= 4 ? near : far) += spectrum.PowerDensity[k];
        }
        return far / near;
    };
    EXPECT_LT(far_leakage(Window::Blackman), far_leakage(Window::Rectangular) / 100);
}

TEST(GuidedPresets, AllParseAndCarryNotes) {
    const std::filesystem::path directory{SIGGEN_PRESET_DIR};
    ASSERT_TRUE(std::filesystem::is_directory(directory));
    int count = 0;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".preset") {
            continue;
        }
        ++count;
        EXPECT_NO_THROW(LoadPreset(entry.path())) << entry.path();
        EXPECT_FALSE(LoadPresetNotes(entry.path()).empty()) << entry.path();
        EXPECT_NO_THROW(Generate(LoadPreset(entry.path()))) << entry.path();
    }
    EXPECT_GE(count, 10);
}

TEST(GuidedPresets, NotesAreIgnoredByTheParser) {
    const auto text = SerializePreset({});
    const auto header_end = text.find('\n') + 1;
    const auto with_notes = text.substr(0, header_end) + "# First line\n#\n#   indented\r\n" + text.substr(header_end);
    EXPECT_EQ(ParsePreset(with_notes), GenerationConfig{});
    EXPECT_EQ(PresetNotes(with_notes), "First line\n\n  indented");
    EXPECT_EQ(PresetNotes(text), "");
    EXPECT_THROW(LoadPresetNotes("/nonexistent-iq-preset/file"), std::runtime_error);
}

} // namespace Core
