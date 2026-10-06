#include "generator.h"
#include "signal_processing.h"
#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
using namespace iq;
TEST(Generator, Mapping) {
    EXPECT_EQ(map_symbols(Modulation::BPSK, "01"), (std::vector<std::complex<float>>{{1,0},{-1,0}}));
    auto q = map_symbols(Modulation::QPSK, "00011110");
    const float a = 1 / std::sqrt(2.f);
    EXPECT_EQ(q, (std::vector<std::complex<float>>{{a,a},{a,-a},{-a,-a},{-a,a}}));
    EXPECT_THROW(map_symbols(Modulation::QPSK, "0"), std::invalid_argument);
    EXPECT_THROW(map_symbols(Modulation::BPSK, "2"), std::invalid_argument);
}
TEST(Generator, DeterministicKnownSeed) {
    GenerationConfig c;
    c.symbol_count = 5;
    auto a = generate(c);
    EXPECT_EQ(a.samples, generate(c).samples);
    // First mt19937(5489) outputs: 3499211612,581869302,3890346734,3586334585,545404204.
    EXPECT_EQ(a.symbols, map_symbols(Modulation::BPSK, "00010"));
    c.seed++;
    EXPECT_NE(a.samples, generate(c).samples);
}
TEST(Generator, PulseLengthDelayAndGain) {
    GenerationConfig c;
    c.symbol_count = 1;
    auto r = generate(c);
    auto h = RRCFilter(c.roll_off, c.span_symbols, c.samples_per_symbol);
    ASSERT_EQ(r.samples.size(), h.size());
    EXPECT_EQ(r.filter_delay_samples, 40u);
    EXPECT_DOUBLE_EQ(r.sample_rate_hz, 8000);
    for (std::size_t i = 0; i < h.size(); ++i) EXPECT_FLOAT_EQ(r.samples[i].real(), static_cast<float>(h[i]));
    c.symbol_count = 17;
    EXPECT_EQ(generate(c).samples.size(), 16u * 8 + 81);
    c.pulse = Pulse::Rectangular;
    c.data_source = DataSource::Explicit;
    c.symbol_count = 2;
    c.bits = "01";
    c.amplitude_gain = 2;
    r = generate(c);
    EXPECT_EQ(r.samples.size(), 16u);
    EXPECT_EQ(r.filter_delay_samples, 0u);
    for (std::size_t i = 0; i < r.samples.size(); ++i) EXPECT_EQ(r.samples[i], std::complex<float>(i < 8 ? 2.f : -2.f, 0));
}
TEST(Generator, InvalidConfiguration) {
    GenerationConfig c;
    c.data_source = DataSource::Explicit;
    EXPECT_THROW(generate(c), std::invalid_argument);
    c.bits = std::string(256, '0');
    EXPECT_NO_THROW(generate(c));
    c.bits += '0';
    EXPECT_THROW(generate(c), std::invalid_argument);
    c = {}; c.symbol_count = 0; EXPECT_THROW(generate(c), std::invalid_argument);
    c = {}; c.symbol_rate_baud = NAN; EXPECT_THROW(generate(c), std::invalid_argument);
    c = {}; c.amplitude_gain = INFINITY; EXPECT_THROW(generate(c), std::invalid_argument);
    c = {}; c.modulation = static_cast<Modulation>(99); EXPECT_THROW(generate(c), std::invalid_argument);
    c = {}; c.samples_per_symbol = 1; EXPECT_THROW(generate(c), std::invalid_argument);
}
#include <bit>
#include <set>
TEST(Generator, QAM16AllPointsGrayAdjacencyAndEnergy) {
    std::string bits;
    for (unsigned word = 0; word < 16; ++word)
        for (int b = 3; b >= 0; --b) bits += (word & (1u << b)) ? '1' : '0';
    const auto points = map_symbols(Modulation::QAM16, bits);
    std::set<std::pair<float,float>> unique;
    double energy = 0;
    const float levels[] = {-3,-1,3,1};
    for (unsigned i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(points[i].real(), levels[i >> 2] / std::sqrt(10.f));
        EXPECT_FLOAT_EQ(points[i].imag(), levels[i & 3] / std::sqrt(10.f));
        unique.emplace(points[i].real(), points[i].imag());
        energy += std::norm(points[i]);
        for (unsigned j = i+1; j < 16; ++j)
            if (std::abs(std::abs(points[i]-points[j]) - 2 / std::sqrt(10.f)) < 1e-6) {
                EXPECT_EQ(std::popcount(i ^ j), 1);
            }
    }
    EXPECT_EQ(unique.size(), 16u);
    EXPECT_NEAR(energy / 16, 1, 1e-7);
}
TEST(Generator, PSK8AllPointsGrayAdjacencyAndEnergy) {
    std::string bits;
    for (unsigned word = 0; word < 8; ++word)
        for (int b = 2; b >= 0; --b) bits += (word & (1u << b)) ? '1' : '0';
    const auto points = map_symbols(Modulation::PSK8, bits);
    ASSERT_EQ(points.size(), 8u);
    // Labels 000,001,011,010,110,111,101,100 sit at phases k*pi/4, k=0..7.
    const int phase_of_word[8] = {0, 1, 3, 2, 7, 6, 4, 5};
    std::set<std::pair<float,float>> unique;
    double energy = 0;
    for (unsigned word = 0; word < 8; ++word) {
        const auto angle = phase_of_word[word] * std::numbers::pi / 4;
        EXPECT_NEAR(points[word].real(), std::cos(angle), 1e-6);
        EXPECT_NEAR(points[word].imag(), std::sin(angle), 1e-6);
        EXPECT_NEAR(std::norm(points[word]), 1., 1e-6);
        unique.emplace(points[word].real(), points[word].imag());
        energy += std::norm(points[word]);
    }
    EXPECT_EQ(unique.size(), 8u);
    EXPECT_NEAR(energy / 8, 1, 1e-6);
    // Nearest neighbours (angular distance pi/4, including wrap-around) differ in exactly one bit.
    for (unsigned a = 0; a < 8; ++a)
        for (unsigned b = a + 1; b < 8; ++b) {
            const auto separation = std::abs(phase_of_word[a] - phase_of_word[b]);
            const auto distance = std::min(separation, 8 - separation);
            if (distance == 1) {
                EXPECT_EQ(std::popcount(a ^ b), 1u) << a << " vs " << b;
            }
        }
    EXPECT_THROW(map_symbols(Modulation::PSK8, "01"), std::invalid_argument);
}
TEST(Generator, QAM64AllPointsGrayAdjacencyAndEnergy) {
    std::string bits;
    for (unsigned word = 0; word < 64; ++word)
        for (int b = 5; b >= 0; --b) bits += (word & (1u << b)) ? '1' : '0';
    const auto points = map_symbols(Modulation::QAM64, bits);
    ASSERT_EQ(points.size(), 64u);
    // Axis levels by three-bit label: 000,-7; 001,-5; 011,-3; 010,-1; 110,+1; 111,+3; 101,+5; 100,+7.
    const float axis[8] = {-7, -5, -1, -3, 7, 5, 1, 3};
    const float scale = 1 / std::sqrt(42.f);
    std::set<std::pair<float,float>> unique;
    double energy = 0;
    for (unsigned word = 0; word < 64; ++word) {
        EXPECT_FLOAT_EQ(points[word].real(), axis[word >> 3] * scale);
        EXPECT_FLOAT_EQ(points[word].imag(), axis[word & 7] * scale);
        unique.emplace(points[word].real(), points[word].imag());
        energy += std::norm(points[word]);
    }
    EXPECT_EQ(unique.size(), 64u);
    EXPECT_NEAR(energy / 64, 1, 1e-5);
    // Nearest neighbours (one axis step of 2/sqrt(42)) differ in exactly one bit.
    const auto step = 2 / std::sqrt(42.f);
    for (unsigned a = 0; a < 64; ++a)
        for (unsigned b = a + 1; b < 64; ++b)
            if (std::abs(std::abs(points[a] - points[b]) - step) < 1e-6) {
                EXPECT_EQ(std::popcount(a ^ b), 1u) << a << " vs " << b;
            }
    EXPECT_THROW(map_symbols(Modulation::QAM64, "01010"), std::invalid_argument);
}
TEST(Generator, SixBitExplicitInputLimits) {
    GenerationConfig c;
    c.modulation = Modulation::QAM64;
    c.data_source = DataSource::Explicit;
    c.symbol_count = 65536;
    c.bits.assign(65536 * 6, '0');
    EXPECT_LT(c.bits.size(), MAX_EXPLICIT_BITS); // Eight-bit 256-QAM sets the ceiling.
    EXPECT_NO_THROW(validate(c));
    c.pulse = Pulse::Rectangular;
    c.samples_per_symbol = 1;
    const auto maximum = generate(c);
    EXPECT_EQ(maximum.symbols.size(), 65536u);
    EXPECT_EQ(maximum.samples.size(), 65536u);
    c.bits += '0';
    EXPECT_THROW(validate(c), std::invalid_argument);
    c.bits.assign(MAX_EXPLICIT_BITS + 1, '0');
    EXPECT_THROW(validate(c), std::invalid_argument);
    c.bits = std::string(65535 * 6, '0');
    EXPECT_THROW(validate(c), std::invalid_argument);
    EXPECT_THROW(map_symbols(Modulation::WGN, "0"), std::invalid_argument);
}
TEST(Generator, FamilyTransitionsResetHiddenSettings) {
    GenerationConfig c;
    c.awgn.enabled = true;
    c.data_source = DataSource::Explicit;
    c.bits.assign(256, '1');
    c.amplitude_gain = 2;
    c.seed = 42;
    c.noise_seed = 43;
    select_waveform(c, Modulation::WGN);
    EXPECT_NO_THROW(validate(c));
    EXPECT_FALSE(c.awgn.enabled);
    EXPECT_EQ(c.data_source, DataSource::Random);
    EXPECT_TRUE(c.bits.empty());
    EXPECT_EQ(c.amplitude_gain, 2);
    EXPECT_EQ(c.seed, 42u);
    EXPECT_EQ(c.noise_seed, 43u);
    c.symbol_count = 17; // Dormant preset fields must not become active.
    select_waveform(c, Modulation::QAM64);
    EXPECT_EQ(c.symbol_count, 256);
    EXPECT_NO_THROW(validate(c));
}
TEST(Generator, WGNSourceDefaultsValidationAndDeterminism) {
    GenerationConfig c;
    c.modulation = Modulation::WGN;
    EXPECT_NO_THROW(validate(c));
    const auto r = generate(c);
    EXPECT_EQ(r.family, Family::Noise);
    EXPECT_EQ(r.samples.size(), 2048u);
    EXPECT_DOUBLE_EQ(r.sample_rate_hz, 8000);
    EXPECT_EQ(r.filter_delay_samples, 0u);
    EXPECT_TRUE(r.symbols.empty());
    EXPECT_EQ(generate(c).samples, r.samples);
    EXPECT_EQ(r.noise.noise_seed, c.noise_seed);
    auto other = c;
    other.noise_seed++;
    EXPECT_NE(generate(other).samples, r.samples);
    // The data seed does not influence the noise source.
    other = c;
    other.seed += 77;
    EXPECT_EQ(generate(other).samples, r.samples);
    // Gain scales the noise amplitude; reported power includes gain squared.
    other = c;
    other.amplitude_gain = 2;
    const auto scaled = generate(other);
    for (std::size_t k = 0; k < r.samples.size(); ++k)
        EXPECT_EQ(scaled.samples[k], r.samples[k] * 2.f);
    // Incompatible settings fail.
    other = c; other.awgn.enabled = true; EXPECT_THROW(validate(other), std::invalid_argument);
    other = c; other.noise_source.sample_count = 0; EXPECT_THROW(validate(other), std::invalid_argument);
    other = c; other.noise_source.sample_rate_hz = 0; EXPECT_THROW(validate(other), std::invalid_argument);
    other = c; other.noise_source.noise_power = -1; EXPECT_THROW(validate(other), std::invalid_argument);
    other = c; other.noise_source.noise_power = NAN; EXPECT_THROW(validate(other), std::invalid_argument);
}
