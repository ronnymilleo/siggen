#include "measurements.h"
#include "pipeline.h"
#include <cmath>
#include <gtest/gtest.h>

namespace {
iq::GenerationConfig config(iq::Modulation modulation) {
    iq::GenerationConfig c;
    c.modulation = modulation;
    c.symbol_count = 32;
    return c;
}
}

TEST(Pipeline, StagesFollowTheGeneratedSignal) {
    const auto signal = iq::generate(config(iq::Modulation::QPSK));
    const auto stages = iq::pipeline_stages(signal);
    EXPECT_EQ(stages.bits, signal.bits);
    EXPECT_EQ(stages.bits.size(), 32u * 2u);
    EXPECT_EQ(stages.symbols.size(), 32u);
    EXPECT_EQ(stages.upsampled.size(), 32u * 8u);
    EXPECT_EQ(stages.received, signal.samples);
    EXPECT_EQ(stages.shaped, signal.samples); // No noise: the last two stages coincide.
    EXPECT_FALSE(stages.degraded);
    for (std::size_t n = 0; n < stages.upsampled.size(); ++n)
        EXPECT_EQ(stages.upsampled[n], n % 8 == 0 ? stages.symbols[n / 8] : std::complex<float>{}) << n;
}

TEST(Pipeline, UpsamplingAppliesGainAndZeros) {
    auto c = config(iq::Modulation::QAM16);
    c.amplitude_gain = 2.5;
    const auto stages = iq::pipeline_stages(iq::generate(c));
    const auto signal = iq::generate(c);
    for (std::size_t k = 0; k < stages.symbols.size(); ++k) EXPECT_EQ(stages.symbols[k], signal.symbols[k] * 2.5f);
}

TEST(Pipeline, ShapedStageIsTheCleanFilterOutput) {
    auto noisy = config(iq::Modulation::PSK8);
    noisy.awgn.enabled = true;
    noisy.awgn.snr_db = 5;
    auto clean = noisy;
    clean.awgn.enabled = false;
    const auto stages = iq::pipeline_stages(iq::generate(noisy));
    EXPECT_TRUE(stages.degraded);
    EXPECT_EQ(stages.shaped, iq::generate(clean).samples);
    EXPECT_NE(stages.received, stages.shaped);
}

TEST(Pipeline, ImpairmentsAloneAlsoDegrade) {
    auto c = config(iq::Modulation::QPSK);
    c.impairments.cfo_hz = 25;
    const auto stages = iq::pipeline_stages(iq::generate(c));
    EXPECT_TRUE(stages.degraded);
    EXPECT_NE(stages.received, stages.shaped);
}

TEST(Pipeline, OqpskDelaysTheQuadratureImpulses) {
    const auto stages = iq::pipeline_stages(iq::generate(config(iq::Modulation::OQPSK)));
    EXPECT_EQ(stages.quadrature_delay_samples, 4u);
    EXPECT_EQ(stages.upsampled.size(), 32u * 8u + 4u);
    for (std::size_t k = 0; k < 32; ++k) {
        EXPECT_EQ(stages.upsampled[k * 8].imag(), 0.f);
        EXPECT_EQ(stages.upsampled[k * 8 + 4].real(), 0.f);
        EXPECT_EQ(stages.upsampled[k * 8].real(), stages.symbols[k].real());
        EXPECT_EQ(stages.upsampled[k * 8 + 4].imag(), stages.symbols[k].imag());
    }
}

TEST(Pipeline, FilterPeaksSitAtTheGroupDelay) {
    auto c = config(iq::Modulation::BPSK);
    c.symbol_count = 1;
    const auto signal = iq::generate(c);
    const auto stages = iq::pipeline_stages(signal);
    EXPECT_EQ(stages.filter_delay_samples, static_cast<std::size_t>(c.span_symbols * c.samples_per_symbol / 2));
    std::size_t peak = 0;
    for (std::size_t n = 0; n < stages.shaped.size(); ++n)
        if (std::abs(stages.shaped[n]) > std::abs(stages.shaped[peak])) peak = n;
    EXPECT_EQ(peak, stages.filter_delay_samples);
}

TEST(Pipeline, RectangularPulseHoldsEachSymbol) {
    auto c = config(iq::Modulation::QPSK);
    c.pulse = iq::Pulse::Rectangular;
    const auto stages = iq::pipeline_stages(iq::generate(c));
    EXPECT_EQ(stages.filter_delay_samples, 0u);
    for (std::size_t n = 0; n < stages.shaped.size(); ++n) EXPECT_EQ(stages.shaped[n], stages.symbols[n / 8]) << n;
}

TEST(Pipeline, RejectsNoiseAndFskSources) {
    for (auto modulation : {iq::Modulation::WGN, iq::Modulation::FSK2, iq::Modulation::MSK})
        EXPECT_THROW(iq::pipeline_stages(iq::generate(config(modulation))), std::invalid_argument);
}

TEST(Pipeline, ExplicitBitsAreKept) {
    auto c = config(iq::Modulation::BPSK);
    c.symbol_count = 4;
    c.data_source = iq::DataSource::Explicit;
    c.bits = "1010";
    EXPECT_EQ(iq::pipeline_stages(iq::generate(c)).bits, "1010");
}

TEST(EnergyRatios, SnrConvertsToEsN0AndEbN0) {
    const auto r = iq::snr_to_energy_ratios(10, 8, 4);
    EXPECT_NEAR(r.es_n0_db, 10 + 10 * std::log10(8.0), 1e-12);
    EXPECT_NEAR(r.eb_n0_db, r.es_n0_db - 10 * std::log10(4.0), 1e-12);
    const auto bpsk = iq::snr_to_energy_ratios(3, 1, 1);
    EXPECT_NEAR(bpsk.es_n0_db, 3, 1e-12);
    EXPECT_NEAR(bpsk.eb_n0_db, 3, 1e-12);
    EXPECT_THROW(iq::snr_to_energy_ratios(0, 0, 1), std::invalid_argument);
    EXPECT_THROW(iq::snr_to_energy_ratios(0, 1, 0), std::invalid_argument);
}

TEST(EnergyRatios, MatchesMeasuredSymbolEnergyOverNoiseDensity) {
    // Es/N0 from the conversion equals (Ps / Rs) / (Pn / Fs) with the measured noise power.
    auto c = config(iq::Modulation::QPSK);
    c.symbol_count = 2048;
    c.awgn.enabled = true;
    c.awgn.snr_db = 7;
    const auto signal = iq::generate(c);
    const auto ratios = iq::snr_to_energy_ratios(7, c.samples_per_symbol, 2);
    const auto es = signal.noise.reference_power / c.symbol_rate_baud;
    const auto n0 = signal.noise.added_noise_power / signal.sample_rate_hz;
    EXPECT_NEAR(ratios.es_n0_db, 10 * std::log10(es / n0), 1e-9);
}
