/**
 * @file    pipeline_test.cpp
 * @brief   Tests for the transmitter pipeline stages and the SNR to Es/N0 and Eb/N0 conversion.
 */

#include "pipeline.h"

#include "measurements.h"
#include <cmath>
#include <gtest/gtest.h>

namespace Core {

namespace {

GenerationConfig Config(Modulation modulation) {
    GenerationConfig config;
    config.Modulation = modulation;
    config.SymbolCount = 32;
    return config;
}

} // namespace

TEST(Pipeline, StagesFollowTheGeneratedSignal) {
    const auto signal = Generate(Config(Modulation::QPSK));
    const auto stages = BuildPipelineStages(signal);
    EXPECT_EQ(stages.Bits, signal.Bits);
    EXPECT_EQ(stages.Bits.size(), 32u * 2u);
    EXPECT_EQ(stages.Symbols.size(), 32u);
    EXPECT_EQ(stages.Upsampled.size(), 32u * 8u);
    EXPECT_EQ(stages.Received, signal.Samples);
    EXPECT_EQ(stages.Shaped, signal.Samples); // No noise: the last two stages coincide.
    EXPECT_FALSE(stages.Degraded);
    for (std::size_t n = 0; n < stages.Upsampled.size(); ++n) {
        EXPECT_EQ(stages.Upsampled[n], n % 8 == 0 ? stages.Symbols[n / 8] : std::complex<float>{}) << n;
    }
}

TEST(Pipeline, UpsamplingAppliesGainAndZeros) {
    auto config = Config(Modulation::QAM16);
    config.AmplitudeGain = 2.5;
    const auto stages = BuildPipelineStages(Generate(config));
    const auto signal = Generate(config);
    for (std::size_t k = 0; k < stages.Symbols.size(); ++k) {
        EXPECT_EQ(stages.Symbols[k], signal.Symbols[k] * 2.5f);
    }
}

TEST(Pipeline, ShapedStageIsTheCleanFilterOutput) {
    auto noisy = Config(Modulation::PSK8);
    noisy.Awgn.Enabled = true;
    noisy.Awgn.SnrDb = 5;
    auto clean = noisy;
    clean.Awgn.Enabled = false;
    const auto stages = BuildPipelineStages(Generate(noisy));
    EXPECT_TRUE(stages.Degraded);
    EXPECT_EQ(stages.Shaped, Generate(clean).Samples);
    EXPECT_NE(stages.Received, stages.Shaped);
}

TEST(Pipeline, ImpairmentsAloneAlsoDegrade) {
    auto config = Config(Modulation::QPSK);
    config.Impairments.CfoHz = 25;
    const auto stages = BuildPipelineStages(Generate(config));
    EXPECT_TRUE(stages.Degraded);
    EXPECT_NE(stages.Received, stages.Shaped);
}

TEST(Pipeline, OqpskDelaysTheQuadratureImpulses) {
    const auto stages = BuildPipelineStages(Generate(Config(Modulation::OQPSK)));
    EXPECT_EQ(stages.QuadratureDelaySamples, 4u);
    EXPECT_EQ(stages.Upsampled.size(), 32u * 8u + 4u);
    for (std::size_t k = 0; k < 32; ++k) {
        EXPECT_EQ(stages.Upsampled[k * 8].imag(), 0.f);
        EXPECT_EQ(stages.Upsampled[k * 8 + 4].real(), 0.f);
        EXPECT_EQ(stages.Upsampled[k * 8].real(), stages.Symbols[k].real());
        EXPECT_EQ(stages.Upsampled[k * 8 + 4].imag(), stages.Symbols[k].imag());
    }
}

TEST(Pipeline, FilterPeaksSitAtTheGroupDelay) {
    auto config = Config(Modulation::BPSK);
    config.SymbolCount = 1;
    const auto signal = Generate(config);
    const auto stages = BuildPipelineStages(signal);
    EXPECT_EQ(stages.FilterDelaySamples, static_cast<std::size_t>(config.SpanSymbols * config.SamplesPerSymbol / 2));
    std::size_t peak = 0;
    for (std::size_t n = 0; n < stages.Shaped.size(); ++n) {
        if (std::abs(stages.Shaped[n]) > std::abs(stages.Shaped[peak])) {
            peak = n;
        }
    }
    EXPECT_EQ(peak, stages.FilterDelaySamples);
}

TEST(Pipeline, RectangularPulseHoldsEachSymbol) {
    auto config = Config(Modulation::QPSK);
    config.Pulse = Pulse::Rectangular;
    const auto stages = BuildPipelineStages(Generate(config));
    EXPECT_EQ(stages.FilterDelaySamples, 0u);
    for (std::size_t n = 0; n < stages.Shaped.size(); ++n) {
        EXPECT_EQ(stages.Shaped[n], stages.Symbols[n / 8]) << n;
    }
}

TEST(Pipeline, RejectsNoiseAndFskSources) {
    for (auto modulation : {Modulation::WGN, Modulation::FSK2, Modulation::MSK}) {
        EXPECT_THROW(BuildPipelineStages(Generate(Config(modulation))), std::invalid_argument);
    }
}

TEST(Pipeline, ExplicitBitsAreKept) {
    auto config = Config(Modulation::BPSK);
    config.SymbolCount = 4;
    config.DataSource = DataSource::Explicit;
    config.Bits = "1010";
    EXPECT_EQ(BuildPipelineStages(Generate(config)).Bits, "1010");
}

TEST(EnergyRatios, SnrConvertsToEsN0AndEbN0) {
    const auto ratios = SnrToEnergyRatios(10, 8, 4);
    EXPECT_NEAR(ratios.EsN0Db, 10 + 10 * std::log10(8.0), 1e-12);
    EXPECT_NEAR(ratios.EbN0Db, ratios.EsN0Db - 10 * std::log10(4.0), 1e-12);
    const auto bpsk = SnrToEnergyRatios(3, 1, 1);
    EXPECT_NEAR(bpsk.EsN0Db, 3, 1e-12);
    EXPECT_NEAR(bpsk.EbN0Db, 3, 1e-12);
    EXPECT_THROW(SnrToEnergyRatios(0, 0, 1), std::invalid_argument);
    EXPECT_THROW(SnrToEnergyRatios(0, 1, 0), std::invalid_argument);
}

TEST(EnergyRatios, MatchesMeasuredSymbolEnergyOverNoiseDensity) {
    // Es/N0 from the conversion equals (Ps / Rs) / (Pn / Fs) with the measured noise power.
    auto config = Config(Modulation::QPSK);
    config.SymbolCount = 2048;
    config.Awgn.Enabled = true;
    config.Awgn.SnrDb = 7;
    const auto signal = Generate(config);
    const auto ratios = SnrToEnergyRatios(7, config.SamplesPerSymbol, 2);
    const auto es = signal.Noise.ReferencePower / config.SymbolRateBaud;
    const auto n0 = signal.Noise.AddedNoisePower / signal.SampleRateHz;
    EXPECT_NEAR(ratios.EsN0Db, 10 * std::log10(es / n0), 1e-9);
}

} // namespace Core
