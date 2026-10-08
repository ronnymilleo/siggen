/**
 * @file    cross_offset_modulation_test.cpp
 * @brief   Tests for 32-QAM, OQPSK, pi/4-DQPSK, 8-DPSK and 4-ASK mapping, generation and integration.
 */

#include "batch.h"

#include "iq_export.h"
#include "measurements.h"
#include "preset.h"
#include "signal_analysis.h"
#include <bit>
#include <cmath>
#include <complex>
#include <filesystem>
#include <gtest/gtest.h>
#include <numbers>
#include <set>
#include <string>

namespace Core {

namespace {

using Sym = std::complex<float>;

// Bit label of value, most significant bit first
std::string Label(unsigned value, int bits) {
    std::string text;
    for (int k = bits - 1; k >= 0; --k) {
        text += ((value >> k) & 1u) ? '1' : '0';
    }
    return text;
}

// Full constellation of a memoryless mapper, one point per bit label
std::vector<Sym> Constellation(Modulation modulation) {
    const int bits_per_symbol = BitsPerSymbol(modulation);
    std::vector<Sym> points;
    for (unsigned v = 0; v < (1u << bits_per_symbol); ++v) {
        points.push_back(MapSymbols(modulation, Label(v, bits_per_symbol))[0]);
    }
    return points;
}

} // namespace

TEST(Qam32, CrossConstellationAndEnergy) {
    const auto points = Constellation(Modulation::QAM32);
    ASSERT_EQ(points.size(), 32u);
    const float scale = std::sqrt(20.f);
    std::set<std::pair<int, int>> grid;
    double energy = 0;
    for (auto point : points) {
        const auto x = std::lround(point.real() * scale), y = std::lround(point.imag() * scale);
        EXPECT_NEAR(point.real() * scale, static_cast<float>(x), 1e-5);
        EXPECT_NEAR(point.imag() * scale, static_cast<float>(y), 1e-5);
        EXPECT_EQ(std::abs(x) % 2, 1);
        EXPECT_EQ(std::abs(y) % 2, 1);
        EXPECT_LE(std::abs(x), 5);
        EXPECT_LE(std::abs(y), 5);
        EXPECT_FALSE(std::abs(x) == 5 && std::abs(y) == 5) << "corner point present";
        grid.emplace(x, y);
        energy += std::norm(point);
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
    const auto points = Constellation(Modulation::QAM32);
    const float step = 2 / std::sqrt(20.f);
    int pairs = 0, one_bit = 0;
    for (unsigned a = 0; a < 32; ++a) {
        for (unsigned b = a + 1; b < 32; ++b) {
            if (std::abs(points[a] - points[b]) > step * 1.01f) {
                continue;
            }
            ++pairs;
            const auto distance = std::popcount(a ^ b);
            EXPECT_LE(distance, 2) << a << " " << b;
            if (distance == 1) {
                ++one_bit;
            }
            const bool across_axis =
                (points[a].real() * points[b].real() < 0) || (points[a].imag() * points[b].imag() < 0);
            if (across_axis) {
                EXPECT_EQ(distance, 1) << a << " " << b;
            }
        }
    }
    EXPECT_GT(pairs, 40);
    EXPECT_GT(one_bit * 100, pairs * 75); // At least three quarters of neighbours are one bit apart.
}

TEST(Qam32, ExplicitInputAndLimits) {
    EXPECT_THROW(MapSymbols(Modulation::QAM32, "0000"), std::invalid_argument);
    GenerationConfig config;
    config.Modulation = Modulation::QAM32;
    config.SymbolCount = 4;
    config.DataSource = DataSource::Explicit;
    config.Bits = "00000"
                  "00001"
                  "00010"
                  "10000";
    const auto result = Generate(config);
    ASSERT_EQ(result.Symbols.size(), 4u);
    const float scale = 1 / std::sqrt(20.f);
    EXPECT_NEAR(result.Symbols[0].real(), 1 * scale, 1e-6);
    EXPECT_NEAR(result.Symbols[1].real(), 3 * scale, 1e-6); // Label 001 -> (3,1).
    EXPECT_NEAR(result.Symbols[2].real(), 5 * scale, 1e-6); // Label 010 -> (5,1).
    EXPECT_NEAR(result.Symbols[2].imag(), 1 * scale, 1e-6);
    EXPECT_NEAR(result.Symbols[3].real(), 1 * scale, 1e-6); // Quadrant 10 mirrors Q: (1,-1).
    EXPECT_NEAR(result.Symbols[3].imag(), -1 * scale, 1e-6);
    config.Bits.pop_back();
    EXPECT_THROW(Generate(config), std::invalid_argument);
}

TEST(PiFourDqpsk, PhaseIncrementsAndEnvelope) {
    const auto mapped = MapSymbols(Modulation::PI4DQPSK, "00"
                                                         "01"
                                                         "11"
                                                         "10");
    const float pi4 = std::numbers::pi_v<float> / 4;
    // Reference phase 0: +45, +135, -135, -45 degrees accumulate to 45, 180, 45, 0.
    const float expected_angle[4] = {pi4, 4 * pi4, pi4, 0};
    for (int k = 0; k < 4; ++k) {
        EXPECT_NEAR(std::abs(mapped[static_cast<std::size_t>(k)]), 1, 1e-6);
        EXPECT_LT(std::abs(mapped[static_cast<std::size_t>(k)] - std::polar(1.f, expected_angle[k])), 1e-5f);
    }
    GenerationConfig config;
    config.Modulation = Modulation::PI4DQPSK;
    config.SymbolCount = 300;
    const auto symbols = Generate(config).Symbols;
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
    config.SymbolCount = 400;
    const auto samples = Generate(config).Samples;
    double minimum = 1e9;
    for (auto x : samples) {
        minimum = std::min<double>(minimum, std::abs(x));
    }
    EXPECT_GT(minimum, 0.0);
}

TEST(Oqpsk, QuadratureLagsHalfASymbol) {
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular}) {
        GenerationConfig qpsk;
        qpsk.Modulation = Modulation::QPSK;
        qpsk.Pulse = pulse;
        qpsk.SymbolCount = 64;
        auto oqpsk = qpsk;
        oqpsk.Modulation = Modulation::OQPSK;
        const auto base = Generate(qpsk), offset = Generate(oqpsk);
        const auto delay = static_cast<std::size_t>(qpsk.SamplesPerSymbol) / 2;
        EXPECT_EQ(QuadratureDelaySamples(oqpsk), delay);
        EXPECT_EQ(QuadratureDelaySamples(qpsk), 0u);
        EXPECT_EQ(base.Symbols, offset.Symbols); // Same bits, same mapping.
        ASSERT_EQ(offset.Samples.size(), base.Samples.size() + delay);
        EXPECT_EQ(offset.FilterDelaySamples, base.FilterDelaySamples);
        for (std::size_t n = 0; n < base.Samples.size(); ++n) {
            EXPECT_FLOAT_EQ(offset.Samples[n].real(), base.Samples[n].real());
            EXPECT_FLOAT_EQ(offset.Samples[n + delay].imag(), base.Samples[n].imag());
        }
        for (std::size_t n = 0; n < delay; ++n) {
            EXPECT_EQ(offset.Samples[n].imag(), 0.f);
        }
    }
}

TEST(Oqpsk, ValidationAndRecovery) {
    GenerationConfig config;
    config.Modulation = Modulation::OQPSK;
    config.Pulse = Pulse::Rectangular;
    config.SamplesPerSymbol = 3;
    EXPECT_THROW(Validate(config), std::invalid_argument);
    config.SamplesPerSymbol = 1;
    EXPECT_THROW(Validate(config), std::invalid_argument);
    config.SamplesPerSymbol = 4;
    EXPECT_NO_THROW(Validate(config));
    // Matched filtering samples I and Q half a symbol apart and recovers the symbols.
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular}) {
        GenerationConfig oqpsk;
        oqpsk.Modulation = Modulation::OQPSK;
        oqpsk.Pulse = pulse;
        oqpsk.SymbolCount = 128;
        oqpsk.SpanSymbols = 12;
        oqpsk.RollOff = .35;
        const auto result = Generate(oqpsk);
        const auto observed = MatchedSymbols(result);
        ASSERT_FALSE(observed.Values.empty());
        for (std::size_t i = 0; i < observed.Values.size(); ++i) {
            EXPECT_LT(std::abs(observed.Values[i] - result.Symbols[observed.SymbolIndices[i]]), .03f);
        }
        const auto accuracy = MeasureSymbolAccuracy(result);
        ASSERT_TRUE(accuracy);
        EXPECT_LT(accuracy->EvmRms, .03);
        const auto eye = BuildEyeDiagram(result, 20);
        ASSERT_FALSE(eye.InPhase.empty());
        for (std::size_t t = 0; t < eye.InPhase.size(); ++t) {
            EXPECT_NEAR(std::abs(eye.InPhase[t][eye.TimeSymbols.size() / 2]), 1 / std::sqrt(2.), .03);
            EXPECT_NEAR(std::abs(eye.Quadrature[t][eye.TimeSymbols.size() / 2]), 1 / std::sqrt(2.), .03);
        }
    }
}

TEST(Oqpsk, LowerEnvelopeVariationThanQpsk) {
    GenerationConfig qpsk;
    qpsk.Modulation = Modulation::QPSK;
    qpsk.SymbolCount = 2048;
    auto oqpsk = qpsk;
    oqpsk.Modulation = Modulation::OQPSK;
    EXPECT_LT(MeasurePowerStatistics(Generate(oqpsk).Samples).PaprDb,
              MeasurePowerStatistics(Generate(qpsk).Samples).PaprDb - 0.5);
}

TEST(Oqpsk, NoiseExportAndBatchFrames) {
    GenerationConfig config;
    config.Modulation = Modulation::OQPSK;
    config.Awgn.Enabled = true;
    config.Awgn.SnrDb = 12;
    const auto result = Generate(config);
    EXPECT_TRUE(result.Noise.AwgnApplied);
    const auto path = std::filesystem::temp_directory_path() / "siggen-oqpsk-export-test.csv";
    std::filesystem::remove(path);
    std::filesystem::remove(MetadataPath(path));
    EXPECT_NO_THROW(ExportSignal(path, result, ExportFormat::CSV));
    std::filesystem::remove(path);
    std::filesystem::remove(MetadataPath(path));
    for (auto pulse : {Pulse::RRC, Pulse::Rectangular}) {
        GenerationConfig frame_config;
        frame_config.Modulation = Modulation::OQPSK;
        frame_config.Pulse = pulse;
        const auto frame = GenerateFrame(frame_config, 1024, 0, std::nullopt);
        EXPECT_EQ(frame.Samples.size(), 1024u);
    }
}

TEST(Dpsk8, GrayPhaseIncrements) {
    const auto mapped = MapSymbols(Modulation::DPSK8, "000"
                                                      "001"
                                                      "011"
                                                      "010"
                                                      "100");
    // Steps 0, 1, 2, 3, 7 of pi/4 accumulate to phases 0, 1, 3, 6, 5 (units of pi/4).
    const int expected_phase[5] = {0, 1, 3, 6, 5};
    for (std::size_t k = 0; k < 5; ++k) {
        EXPECT_NEAR(std::abs(mapped[k]), 1, 1e-6);
        const auto angle = static_cast<float>(expected_phase[k]) * std::numbers::pi_v<float> / 4;
        EXPECT_LT(std::abs(mapped[k] - std::polar(1.f, angle)), 1e-5f) << k;
    }
    GenerationConfig config;
    config.Modulation = Modulation::DPSK8;
    config.SymbolCount = 200;
    const auto symbols = Generate(config).Symbols;
    const auto rotation = std::polar(1.f, 0.3f); // Less than half a step: a constant rotation must not change the data.
    std::set<int> seen;
    for (std::size_t k = 1; k < symbols.size(); ++k) {
        const auto index = [](float angle) {
            return (static_cast<int>(std::lround(angle / (std::numbers::pi / 4))) + 8) % 8;
        };
        const auto step = index(std::arg(symbols[k] * std::conj(symbols[k - 1])));
        EXPECT_EQ(step, index(std::arg(symbols[k] * rotation * std::conj(symbols[k - 1] * rotation))));
        seen.insert(step);
    }
    EXPECT_EQ(seen.size(), 8u); // Every increment occurs in a long random sequence.
}

TEST(Ask4, UnipolarGrayLevels) {
    const auto points = Constellation(Modulation::ASK4);
    const float scale = 1 / std::sqrt(3.5f);
    // Labels 00, 01, 11, 10 ascend 0, 1, 2, 3.
    EXPECT_NEAR(points[0].real(), 0 * scale, 1e-6);
    EXPECT_NEAR(points[1].real(), 1 * scale, 1e-6);
    EXPECT_NEAR(points[3].real(), 2 * scale, 1e-6);
    EXPECT_NEAR(points[2].real(), 3 * scale, 1e-6);
    double energy = 0, mean = 0;
    for (auto point : points) {
        EXPECT_EQ(point.imag(), 0);
        EXPECT_GE(point.real(), 0);
        energy += std::norm(point);
        mean += point.real();
    }
    EXPECT_NEAR(energy / 4, 1, 1e-6);
    EXPECT_GT(mean, 0); // Non-zero mean: the spectrum has a carrier line, like OOK.
    GenerationConfig config;
    config.Modulation = Modulation::ASK4;
    config.SymbolCount = 256;
    const auto result = Generate(config);
    const auto accuracy = MeasureSymbolAccuracy(result);
    ASSERT_TRUE(accuracy);
    EXPECT_LT(accuracy->EvmRms, .02);
}

TEST(NewModulations, NamesPresetsAndIdentifiers) {
    Modulation parsed{};
    ASSERT_TRUE(ParseModulation("32-qam", parsed));
    EXPECT_EQ(parsed, Modulation::QAM32);
    ASSERT_TRUE(ParseModulation("oqpsk", parsed));
    EXPECT_EQ(parsed, Modulation::OQPSK);
    ASSERT_TRUE(ParseModulation("PI/4-DQPSK", parsed));
    EXPECT_EQ(parsed, Modulation::PI4DQPSK);
    EXPECT_STREQ(ModulationName(Modulation::PI4DQPSK), "pi/4-DQPSK");
    EXPECT_EQ(WaveformId(Modulation::QAM32), 14);
    EXPECT_EQ(WaveformId(Modulation::OQPSK), 15);
    EXPECT_EQ(WaveformId(Modulation::PI4DQPSK), 16);
    EXPECT_EQ(WaveformId(Modulation::DPSK8), 17);
    EXPECT_EQ(WaveformId(Modulation::ASK4), 18);
    ASSERT_TRUE(ParseModulation("8-dpsk", parsed));
    EXPECT_EQ(parsed, Modulation::DPSK8);
    ASSERT_TRUE(ParseModulation("4-ask", parsed));
    EXPECT_EQ(parsed, Modulation::ASK4);
    std::set<int> ids;
    for (const auto &entry : Waveforms()) {
        ids.insert(entry.StableId);
    }
    EXPECT_EQ(ids.size(), Waveforms().size());
    for (auto modulation :
         {Modulation::QAM32, Modulation::OQPSK, Modulation::PI4DQPSK, Modulation::DPSK8, Modulation::ASK4}) {
        GenerationConfig config;
        config.Modulation = modulation;
        config.DataSource = DataSource::Explicit;
        config.Bits = std::string(
            static_cast<std::size_t>(config.SymbolCount) * static_cast<std::size_t>(BitsPerSymbol(modulation)), '1');
        EXPECT_EQ(ParsePreset(SerializePreset(config)), config);
    }
}

} // namespace Core
