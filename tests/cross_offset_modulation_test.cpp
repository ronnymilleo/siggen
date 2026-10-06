#include "batch.h"
#include "iq_export.h"
#include "measurements.h"
#include "preset.h"
#include "signal_analysis.h"
#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <complex>
#include <filesystem>
#include <numbers>
#include <set>
#include <string>

using namespace iq;

namespace {
using Sym = std::complex<float>;
std::string label(unsigned value, int bits) {
    std::string text;
    for (int k = bits - 1; k >= 0; --k) text += ((value >> k) & 1u) ? '1' : '0';
    return text;
}
std::vector<Sym> constellation(Modulation m) {
    const int bps = bits_per_symbol(m);
    std::vector<Sym> points;
    for (unsigned v = 0; v < (1u << bps); ++v) points.push_back(map_symbols(m, label(v, bps))[0]);
    return points;
}
}

TEST(Qam32, CrossConstellationAndEnergy) {
    const auto points = constellation(Modulation::QAM32);
    ASSERT_EQ(points.size(), 32u);
    const float scale = std::sqrt(20.f);
    std::set<std::pair<int, int>> grid;
    double energy = 0;
    for (auto p : points) {
        const auto x = std::lround(p.real() * scale), y = std::lround(p.imag() * scale);
        EXPECT_NEAR(p.real() * scale, static_cast<float>(x), 1e-5);
        EXPECT_NEAR(p.imag() * scale, static_cast<float>(y), 1e-5);
        EXPECT_EQ(std::abs(x) % 2, 1);
        EXPECT_EQ(std::abs(y) % 2, 1);
        EXPECT_LE(std::abs(x), 5);
        EXPECT_LE(std::abs(y), 5);
        EXPECT_FALSE(std::abs(x) == 5 && std::abs(y) == 5) << "corner point present";
        grid.emplace(x, y);
        energy += std::norm(p);
    }
    EXPECT_EQ(grid.size(), 32u); // Every label maps to a different point.
    EXPECT_NEAR(energy / 32, 1, 1e-6);
    // First quadrant starts at (1,1); label 00000 is quadrant 00 point 000.
    EXPECT_NEAR(points[0].real() * scale, 1, 1e-5);
    EXPECT_NEAR(points[0].imag() * scale, 1, 1e-5);
}

TEST(Qam32, NearestNeighboursAreMostlyOneBitApart) {
    // Cross QAM cannot be fully Gray labelled. Every nearest-neighbour pair differs in at most
    // two bits, and every pair across an axis (mirrored quadrants) differs in exactly one.
    const auto points = constellation(Modulation::QAM32);
    const float step = 2 / std::sqrt(20.f);
    int pairs = 0, one_bit = 0;
    for (unsigned a = 0; a < 32; ++a)
        for (unsigned b = a + 1; b < 32; ++b) {
            if (std::abs(points[a] - points[b]) > step * 1.01f) continue;
            ++pairs;
            const auto distance = std::popcount(a ^ b);
            EXPECT_LE(distance, 2) << a << " " << b;
            if (distance == 1) ++one_bit;
            const bool across_axis = (points[a].real() * points[b].real() < 0) || (points[a].imag() * points[b].imag() < 0);
            if (across_axis) { EXPECT_EQ(distance, 1) << a << " " << b; }
        }
    EXPECT_GT(pairs, 40);
    EXPECT_GT(one_bit * 100, pairs * 75); // At least three quarters of neighbours are one bit apart.
}

TEST(Qam32, ExplicitInputAndLimits) {
    EXPECT_THROW(map_symbols(Modulation::QAM32, "0000"), std::invalid_argument);
    GenerationConfig c;
    c.modulation = Modulation::QAM32;
    c.symbol_count = 4;
    c.data_source = DataSource::Explicit;
    c.bits = "00000" "00001" "00010" "10000";
    const auto r = generate(c);
    ASSERT_EQ(r.symbols.size(), 4u);
    const float s = 1 / std::sqrt(20.f);
    EXPECT_NEAR(r.symbols[0].real(), 1 * s, 1e-6);
    EXPECT_NEAR(r.symbols[1].real(), 3 * s, 1e-6);   // Label 001 -> (3,1).
    EXPECT_NEAR(r.symbols[2].real(), 5 * s, 1e-6);   // Label 010 -> (5,1).
    EXPECT_NEAR(r.symbols[2].imag(), 1 * s, 1e-6);
    EXPECT_NEAR(r.symbols[3].real(), 1 * s, 1e-6);   // Quadrant 10 mirrors Q: (1,-1).
    EXPECT_NEAR(r.symbols[3].imag(), -1 * s, 1e-6);
    c.bits.pop_back();
    EXPECT_THROW(generate(c), std::invalid_argument);
}

TEST(PiFourDqpsk, PhaseIncrementsAndEnvelope) {
    const auto s = map_symbols(Modulation::PI4DQPSK, "00" "01" "11" "10");
    const float pi4 = std::numbers::pi_v<float> / 4;
    // Reference phase 0: +45, +135, -135, -45 degrees accumulate to 45, 180, 45, 0.
    const float expected_angle[4] = {pi4, 4 * pi4, pi4, 0};
    for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(std::abs(s[static_cast<std::size_t>(k)]), 1, 1e-6);
        EXPECT_LT(std::abs(s[static_cast<std::size_t>(k)] - std::polar(1.f, expected_angle[k])), 1e-5f);
    }
    GenerationConfig c;
    c.modulation = Modulation::PI4DQPSK;
    c.symbol_count = 300;
    const auto symbols = generate(c).symbols;
    // Decoded increments are odd multiples of pi/4, and the symbol phase alternates between two QPSK sets.
    Sym previous{1, 0};
    for (std::size_t k = 0; k < symbols.size(); ++k) {
        const auto step = std::arg(symbols[k] * std::conj(previous));
        const auto turns = static_cast<int>(std::lround(step / pi4));
        EXPECT_EQ(std::abs(turns) % 2, 1) << k;
        EXPECT_NEAR(step, static_cast<float>(turns) * pi4, 1e-4);
        const auto phase_index = ((static_cast<int>(std::lround(std::arg(symbols[k]) / pi4)) % 8) + 8) % 8;
        EXPECT_EQ(phase_index % 2, static_cast<int>((k + 1) % 2)) << k;
        previous = symbols[k];
    }
    // The envelope never passes through the origin because no transition is 180 degrees.
    c.symbol_count = 400;
    const auto samples = generate(c).samples;
    double minimum = 1e9;
    for (auto x : samples) minimum = std::min<double>(minimum, std::abs(x));
    EXPECT_GT(minimum, 0.0);
}

TEST(Oqpsk, QuadratureLagsHalfASymbol) {
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular}) {
        GenerationConfig q;
        q.modulation = Modulation::QPSK;
        q.pulse = pulse;
        q.symbol_count = 64;
        auto o = q;
        o.modulation = Modulation::OQPSK;
        const auto base = generate(q), offset = generate(o);
        const auto delay = static_cast<std::size_t>(q.samples_per_symbol) / 2;
        EXPECT_EQ(quadrature_delay_samples(o), delay);
        EXPECT_EQ(quadrature_delay_samples(q), 0u);
        EXPECT_EQ(base.symbols, offset.symbols); // Same bits, same mapping.
        ASSERT_EQ(offset.samples.size(), base.samples.size() + delay);
        EXPECT_EQ(offset.filter_delay_samples, base.filter_delay_samples);
        for (std::size_t n = 0; n < base.samples.size(); ++n) {
            EXPECT_FLOAT_EQ(offset.samples[n].real(), base.samples[n].real());
            EXPECT_FLOAT_EQ(offset.samples[n + delay].imag(), base.samples[n].imag());
        }
        for (std::size_t n = 0; n < delay; ++n) EXPECT_EQ(offset.samples[n].imag(), 0.f);
    }
}

TEST(Oqpsk, ValidationAndRecovery) {
    GenerationConfig c;
    c.modulation = Modulation::OQPSK;
    c.pulse = Pulse::Rectangular;
    c.samples_per_symbol = 3;
    EXPECT_THROW(validate(c), std::invalid_argument);
    c.samples_per_symbol = 1;
    EXPECT_THROW(validate(c), std::invalid_argument);
    c.samples_per_symbol = 4;
    EXPECT_NO_THROW(validate(c));
    // Matched filtering samples I and Q half a symbol apart and recovers the symbols.
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular}) {
        GenerationConfig o;
        o.modulation = Modulation::OQPSK;
        o.pulse = pulse;
        o.symbol_count = 128;
        o.span_symbols = 12;
        o.roll_off = .35;
        const auto r = generate(o);
        const auto observed = matched_symbols(r);
        ASSERT_FALSE(observed.values.empty());
        for (std::size_t i = 0; i < observed.values.size(); ++i)
            EXPECT_LT(std::abs(observed.values[i] - r.symbols[observed.symbol_indices[i]]), .03f);
        const auto accuracy = symbol_accuracy(r);
        ASSERT_TRUE(accuracy);
        EXPECT_LT(accuracy->evm_rms, .03);
        const auto eye = eye_diagram(r, 20);
        ASSERT_FALSE(eye.in_phase.empty());
        for (std::size_t t = 0; t < eye.in_phase.size(); ++t) {
            EXPECT_NEAR(std::abs(eye.in_phase[t][eye.time_symbols.size() / 2]), 1 / std::sqrt(2.), .03);
            EXPECT_NEAR(std::abs(eye.quadrature[t][eye.time_symbols.size() / 2]), 1 / std::sqrt(2.), .03);
        }
    }
}

TEST(Oqpsk, LowerEnvelopeVariationThanQpsk) {
    GenerationConfig q;
    q.modulation = Modulation::QPSK;
    q.symbol_count = 2048;
    auto o = q;
    o.modulation = Modulation::OQPSK;
    EXPECT_LT(power_statistics(generate(o).samples).papr_db, power_statistics(generate(q).samples).papr_db - 0.5);
}

TEST(Oqpsk, NoiseExportAndBatchFrames) {
    GenerationConfig c;
    c.modulation = Modulation::OQPSK;
    c.awgn.enabled = true;
    c.awgn.snr_db = 12;
    const auto r = generate(c);
    EXPECT_TRUE(r.noise.awgn_applied);
    const auto path = std::filesystem::temp_directory_path() / "siggen-oqpsk-export-test.csv";
    std::filesystem::remove(path);
    std::filesystem::remove(metadata_path(path));
    EXPECT_NO_THROW(export_signal(path, r, ExportFormat::CSV));
    std::filesystem::remove(path);
    std::filesystem::remove(metadata_path(path));
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular}) {
        GenerationConfig b;
        b.modulation = Modulation::OQPSK;
        b.pulse = pulse;
        const auto frame = generate_frame(b, 1024, 0, std::nullopt);
        EXPECT_EQ(frame.samples.size(), 1024u);
    }
}

TEST(Dpsk8, GrayPhaseIncrements) {
    const auto s = map_symbols(Modulation::DPSK8, "000" "001" "011" "010" "100");
    // Steps 0, 1, 2, 3, 7 of pi/4 accumulate to phases 0, 1, 3, 6, 5 (units of pi/4).
    const int expected_phase[5] = {0, 1, 3, 6, 5};
    for (std::size_t k = 0; k < 5; ++k) {
        EXPECT_NEAR(std::abs(s[k]), 1, 1e-6);
        const auto angle = static_cast<float>(expected_phase[k]) * std::numbers::pi_v<float> / 4;
        EXPECT_LT(std::abs(s[k] - std::polar(1.f, angle)), 1e-5f) << k;
    }
    GenerationConfig c;
    c.modulation = Modulation::DPSK8;
    c.symbol_count = 200;
    const auto symbols = generate(c).symbols;
    const auto rotation = std::polar(1.f, 0.3f); // Less than half a step: a constant rotation must not change the data.
    std::set<int> seen;
    for (std::size_t k = 1; k < symbols.size(); ++k) {
        const auto index = [](float angle) { return (static_cast<int>(std::lround(angle / (std::numbers::pi / 4))) + 8) % 8; };
        const auto step = index(std::arg(symbols[k] * std::conj(symbols[k - 1])));
        EXPECT_EQ(step, index(std::arg(symbols[k] * rotation * std::conj(symbols[k - 1] * rotation))));
        seen.insert(step);
    }
    EXPECT_EQ(seen.size(), 8u); // Every increment occurs in a long random sequence.
}

TEST(Ask4, UnipolarGrayLevels) {
    const auto points = constellation(Modulation::ASK4);
    const float s = 1 / std::sqrt(3.5f);
    // Labels 00, 01, 11, 10 ascend 0, 1, 2, 3.
    EXPECT_NEAR(points[0].real(), 0 * s, 1e-6);
    EXPECT_NEAR(points[1].real(), 1 * s, 1e-6);
    EXPECT_NEAR(points[3].real(), 2 * s, 1e-6);
    EXPECT_NEAR(points[2].real(), 3 * s, 1e-6);
    double energy = 0, mean = 0;
    for (auto p : points) {
        EXPECT_EQ(p.imag(), 0);
        EXPECT_GE(p.real(), 0);
        energy += std::norm(p);
        mean += p.real();
    }
    EXPECT_NEAR(energy / 4, 1, 1e-6);
    EXPECT_GT(mean, 0); // Non-zero mean: the spectrum has a carrier line, like OOK.
    GenerationConfig c;
    c.modulation = Modulation::ASK4;
    c.symbol_count = 256;
    const auto r = generate(c);
    const auto accuracy = symbol_accuracy(r);
    ASSERT_TRUE(accuracy);
    EXPECT_LT(accuracy->evm_rms, .02);
}

TEST(NewModulations, NamesPresetsAndIdentifiers) {
    Modulation parsed{};
    ASSERT_TRUE(parse_modulation("32-qam", parsed));
    EXPECT_EQ(parsed, Modulation::QAM32);
    ASSERT_TRUE(parse_modulation("oqpsk", parsed));
    EXPECT_EQ(parsed, Modulation::OQPSK);
    ASSERT_TRUE(parse_modulation("PI/4-DQPSK", parsed));
    EXPECT_EQ(parsed, Modulation::PI4DQPSK);
    EXPECT_STREQ(modulation_name(Modulation::PI4DQPSK), "pi/4-DQPSK");
    EXPECT_EQ(waveform_id(Modulation::QAM32), 14);
    EXPECT_EQ(waveform_id(Modulation::OQPSK), 15);
    EXPECT_EQ(waveform_id(Modulation::PI4DQPSK), 16);
    EXPECT_EQ(waveform_id(Modulation::DPSK8), 17);
    EXPECT_EQ(waveform_id(Modulation::ASK4), 18);
    ASSERT_TRUE(parse_modulation("8-dpsk", parsed));
    EXPECT_EQ(parsed, Modulation::DPSK8);
    ASSERT_TRUE(parse_modulation("4-ask", parsed));
    EXPECT_EQ(parsed, Modulation::ASK4);
    std::set<int> ids;
    for (const auto& entry : waveforms()) ids.insert(entry.stable_id);
    EXPECT_EQ(ids.size(), waveforms().size());
    for (auto m : {Modulation::QAM32, Modulation::OQPSK, Modulation::PI4DQPSK, Modulation::DPSK8, Modulation::ASK4}) {
        GenerationConfig c;
        c.modulation = m;
        c.data_source = DataSource::Explicit;
        c.bits = std::string(static_cast<std::size_t>(c.symbol_count) * static_cast<std::size_t>(bits_per_symbol(m)), '1');
        EXPECT_EQ(parse_preset(serialize_preset(c)), c);
    }
}
