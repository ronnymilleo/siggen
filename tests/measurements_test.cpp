#include "measurements.h"
#include "preset.h"
#include "signal_analysis.h"
#include <gtest/gtest.h>
#include <cmath>
#include <filesystem>
#include <numbers>

using namespace iq;

TEST(Measurements, PowerStatisticsAndPapr) {
    EXPECT_EQ(power_statistics({}).mean_power, 0);
    const auto zero = power_statistics(std::vector<std::complex<float>>(4));
    EXPECT_EQ(zero.papr_db, 0);
    const auto stats = power_statistics({{1, 0}, {0, 1}, {-1, 0}, {0, 2}});
    EXPECT_DOUBLE_EQ(stats.mean_power, 7. / 4);
    EXPECT_DOUBLE_EQ(stats.peak_power, 4);
    EXPECT_NEAR(stats.papr_db, 10 * std::log10(4 / (7. / 4)), 1e-12);
    EXPECT_THROW(power_statistics({{std::nanf(""), 0}}), std::invalid_argument);
    // Rectangular BPSK has a constant envelope.
    GenerationConfig c;
    c.pulse = Pulse::Rectangular;
    EXPECT_NEAR(power_statistics(generate(c).samples).papr_db, 0, 1e-6);
    // Shaped higher-order QAM has a clearly higher PAPR than shaped BPSK.
    GenerationConfig qam;
    qam.modulation = Modulation::QAM64;
    qam.symbol_count = 1024;
    GenerationConfig bpsk;
    bpsk.symbol_count = 1024;
    EXPECT_GT(power_statistics(generate(qam).samples).papr_db, power_statistics(generate(bpsk).samples).papr_db);
}

TEST(Measurements, CleanSignalHasNearZeroEvm) {
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular}) {
        GenerationConfig c;
        c.modulation = Modulation::QAM16;
        c.pulse = pulse;
        c.amplitude_gain = 3;
        const auto accuracy = symbol_accuracy(generate(c));
        ASSERT_TRUE(accuracy);
        // RRC truncated to 10 symbols at roll-off 0.2 leaves a small residual ISI floor (~-43 dB).
        EXPECT_LT(accuracy->evm_rms, pulse == Pulse::RRC ? 1.5e-2 : 1e-3);
        EXPECT_GT(accuracy->snr_after_matched_db, pulse == Pulse::RRC ? 36 : 55);
        EXPECT_NEAR(accuracy->expected_offset_db, 10 * std::log10(8.), 1e-12);
    }
}

TEST(Measurements, MeasuredSnrTracksRequestedSnr) {
    // Post-matched-filter SNR equals sample SNR + 10 log10(SPS) in expectation.
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular})
        for (double snr : {0., 8., 16.}) {
            GenerationConfig c;
            c.modulation = Modulation::QPSK;
            c.pulse = pulse;
            c.symbol_count = 8192;
            c.awgn.enabled = true;
            c.awgn.snr_db = snr;
            const auto accuracy = symbol_accuracy(generate(c));
            ASSERT_TRUE(accuracy);
            EXPECT_NEAR(accuracy->snr_after_matched_db, snr + accuracy->expected_offset_db, 0.6)
                << "snr " << snr;
            EXPECT_NEAR(accuracy->evm_db, -accuracy->snr_after_matched_db, 1e-12);
        }
}

TEST(Measurements, UnavailableWithoutSymbols) {
    GenerationConfig wgn;
    wgn.modulation = Modulation::WGN;
    EXPECT_FALSE(symbol_accuracy(generate(wgn)));
    EXPECT_THROW(eye_diagram(generate(wgn)), std::invalid_argument);
    GenerationConfig tiny;
    tiny.symbol_count = 1;
    EXPECT_FALSE(symbol_accuracy(generate(tiny)));
    EXPECT_TRUE(eye_diagram(generate(tiny)).in_phase.empty());
    EXPECT_THROW(eye_diagram(generate({}), 0), std::invalid_argument);
}

TEST(Eye, TracesCrossTheDecisionInstantAtTheSymbolValue) {
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular}) {
        GenerationConfig c;
        c.modulation = Modulation::BPSK;
        c.pulse = pulse;
        c.symbol_count = 128;
        const auto r = generate(c);
        const auto eye = eye_diagram(r, 50);
        ASSERT_EQ(eye.time_symbols.size(), 17u);
        EXPECT_DOUBLE_EQ(eye.time_symbols.front(), -1);
        EXPECT_DOUBLE_EQ(eye.time_symbols[8], 0);
        EXPECT_DOUBLE_EQ(eye.time_symbols.back(), 1);
        ASSERT_EQ(eye.in_phase.size(), 50u);
        ASSERT_EQ(eye.quadrature.size(), 50u);
        for (std::size_t t = 0; t < eye.in_phase.size(); ++t) {
            ASSERT_EQ(eye.in_phase[t].size(), 17u);
            // Matched-filter output at the centre is one BPSK symbol: +/-1, Q zero.
            const auto tolerance = pulse == Pulse::RRC ? 2e-2 : 1e-3; // RRC truncation ISI.
            EXPECT_NEAR(std::abs(eye.in_phase[t][8]), 1, tolerance);
            EXPECT_NEAR(eye.quadrature[t][8], 0, tolerance);
        }
    }
    GenerationConfig c;
    c.symbol_count = 20;
    EXPECT_EQ(eye_diagram(generate(c), 1000).in_phase.size(), 0u); // 20 symbols < 2 * span.
    c.symbol_count = 64;
    EXPECT_EQ(eye_diagram(generate(c), 1000).in_phase.size(), 44u);
}

TEST(Windows, ValuesAndPsdDefaults) {
    const auto n = std::size_t{8};
    EXPECT_DOUBLE_EQ(window_value(Window::Hann, 0, n), 0);
    EXPECT_DOUBLE_EQ(window_value(Window::Hann, 4, n), 1);
    EXPECT_NEAR(window_value(Window::Hamming, 0, n), .08, 1e-12);
    EXPECT_NEAR(window_value(Window::Blackman, 0, n), 0, 1e-12);
    EXPECT_NEAR(window_value(Window::Blackman, 4, n), 1, 1e-12);
    EXPECT_EQ(window_value(Window::Rectangular, 3, n), 1);
    // The default window remains periodic Hann.
    GenerationConfig c;
    const auto r = generate(c);
    const auto implicit = welch_psd(r.samples, r.sample_rate_hz);
    const auto hann = welch_psd(r.samples, r.sample_rate_hz, 1024, Window::Hann);
    EXPECT_EQ(implicit.power_density, hann.power_density);
    EXPECT_EQ(hann.window, Window::Hann);
    const auto rect = welch_psd(r.samples, r.sample_rate_hz, 1024, Window::Rectangular);
    EXPECT_NE(rect.power_density, hann.power_density);
    EXPECT_EQ(rect.window, Window::Rectangular);
    // Power is preserved across windows: integrated PSD is within a few percent.
    const auto total = [](const Spectrum& s) {
        double sum = 0;
        for (auto p : s.power_density) sum += p;
        return sum;
    };
    for (auto window : {Window::Hamming, Window::Blackman})
        EXPECT_NEAR(total(welch_psd(r.samples, r.sample_rate_hz, 1024, window)) / total(hann), 1, .15);
}

TEST(Windows, BlackmanHasLowerSidelobesThanRectangular) {
    // A single off-bin tone leaks widely with a rectangular window.
    std::vector<std::complex<float>> tone(4096);
    for (std::size_t k = 0; k < tone.size(); ++k)
        tone[k] = std::polar(1.f, static_cast<float>(2 * std::numbers::pi * 100.37 * k / 1024));
    const auto far_leakage = [&](Window window) {
        const auto s = welch_psd(tone, 1024, 1024, window);
        double near = 0, far = 0;
        for (std::size_t k = 0; k < s.power_density.size(); ++k)
            (std::abs(static_cast<int>(k) - (512 + 100)) <= 4 ? near : far) += s.power_density[k];
        return far / near;
    };
    EXPECT_LT(far_leakage(Window::Blackman), far_leakage(Window::Rectangular) / 100);
}

TEST(GuidedPresets, AllParseAndCarryNotes) {
    const std::filesystem::path directory{SIGGEN_PRESET_DIR};
    ASSERT_TRUE(std::filesystem::is_directory(directory));
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".preset") continue;
        ++count;
        EXPECT_NO_THROW(load_preset(entry.path())) << entry.path();
        EXPECT_FALSE(load_preset_notes(entry.path()).empty()) << entry.path();
        EXPECT_NO_THROW(generate(load_preset(entry.path()))) << entry.path();
    }
    EXPECT_GE(count, 6);
}

TEST(GuidedPresets, NotesAreIgnoredByTheParser) {
    const auto text = serialize_preset({});
    const auto header_end = text.find('\n') + 1;
    const auto with_notes = text.substr(0, header_end) + "# First line\n#\n#   indented\r\n" + text.substr(header_end);
    EXPECT_EQ(parse_preset(with_notes), GenerationConfig{});
    EXPECT_EQ(preset_notes(with_notes), "First line\n\n  indented");
    EXPECT_EQ(preset_notes(text), "");
    EXPECT_THROW(load_preset_notes("/nonexistent-iq-preset/file"), std::runtime_error);
}
