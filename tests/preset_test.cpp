#include "preset.h"
#include <gtest/gtest.h>

namespace {
const char* const V1_PRESET =
    "[IQ Generator Preset]\r\nVersion=1\r\nModulation=QPSK\r\nNumberOfSymbols=32\r\nSymbolRateBaud=1000\r\n"
    "SamplesPerSymbol=8\r\nPulse=RRC\r\nRRCBeta=0.2\r\nSpanSymbols=10\r\nAmplitudeGain=1\r\nDataSource=Random\r\n"
    "Seed=5489\r\nBits=\r\n";
}

TEST(Preset, RoundTripAndLegacyDefaults) {
    for (auto modulation : {iq::Modulation::BPSK, iq::Modulation::QPSK, iq::Modulation::PSK8,
                            iq::Modulation::QAM16, iq::Modulation::QAM64}) {
        iq::GenerationConfig c;
        c.modulation = modulation; c.data_source = iq::DataSource::Explicit;
        c.bits = std::string(static_cast<std::size_t>(c.symbol_count) * iq::bits_per_symbol(modulation), '1');
        c.amplitude_gain = 1.234567890123; c.seed = 4294967295u;
        c.awgn.enabled = true; c.awgn.snr_db = -7.5; c.noise_seed = 99;
        EXPECT_EQ(iq::parse_preset(iq::serialize_preset(c)), c);
    }
    iq::GenerationConfig wgn;
    wgn.modulation = iq::Modulation::WGN;
    wgn.noise_source.sample_count = 4096;
    wgn.noise_source.sample_rate_hz = 12345.6789;
    wgn.noise_source.noise_power = 2.5;
    wgn.noise_seed = 5490;
    EXPECT_EQ(iq::parse_preset(iq::serialize_preset(wgn)), wgn);
    EXPECT_NE(iq::serialize_preset(wgn).find("Version=2"), std::string::npos);
    auto c = iq::parse_preset("[SignalGenerator Preset]\r\nNumberOfSymbols=32\r\nSamplesPerSymbol=4\r\nRRCBeta=0.3\r\nConstellationStart=43\r\n");
    EXPECT_EQ(c.symbol_count, 32); EXPECT_EQ(c.samples_per_symbol, 4);
    EXPECT_EQ(c.symbol_rate_baud, 1000); EXPECT_EQ(c.seed, 5489u); EXPECT_EQ(c.span_symbols, 10);
    // Version-1 imports receive noise-disabled defaults.
    auto v1 = iq::parse_preset(V1_PRESET);
    EXPECT_EQ(v1.modulation, iq::Modulation::QPSK);
    EXPECT_EQ(v1.symbol_count, 32);
    EXPECT_FALSE(v1.awgn.enabled);
    EXPECT_EQ(v1.noise_seed, 5490u);
    EXPECT_EQ(v1.noise_source, iq::NoiseSourceSettings{});
}

TEST(Preset, MalformedAndTransactionalApplication) {
    iq::GenerationConfig current; current.seed = 123;
    const auto before = current;
    const auto text = iq::serialize_preset({});
    for (const auto& change : std::vector<std::pair<std::string,std::string>>{
        {"Version=2", "Version=3"}, {"NumberOfSymbols=256", "NumberOfSymbols=2oops"},
        {"RRCBeta=", "RRCBeta=nan"}, {"Seed=5489", "Seed=-1"},
        {"SamplesPerSymbol=8", "SamplesPerSymbol=0"}, {"Pulse=RRC", "Pulse=Unknown"},
        {"Bits=", "Bits=x"}, {"DataSource=Random", "DataSource=Explicit"},
        {"AwgnEnabled=false", "AwgnEnabled=maybe"}, {"NoiseSeed=5490", "NoiseSeed=-1"},
        {"NoiseSampleCount=2048", "NoiseSampleCount=0"}, {"NoisePower=1", "NoisePower=inf"},
        {"Modulation=BPSK", "Modulation=GFSK"}}) {
        auto bad = text; bad.replace(bad.find(change.first), change.first.size(), change.second);
        EXPECT_THROW(current = iq::parse_preset(bad), std::invalid_argument) << change.first;
        EXPECT_EQ(current, before);
    }
    EXPECT_THROW(iq::parse_preset(text + "Seed=5\n"), std::invalid_argument);
    EXPECT_THROW(iq::parse_preset(text + "Unknown=1\n"), std::invalid_argument);
    EXPECT_THROW(iq::parse_preset(""), std::invalid_argument);
    EXPECT_THROW(iq::parse_preset("[IQ Generator Preset]\nVersion=2\n"), std::invalid_argument);
    // Missing noise fields are rejected in version 2.
    auto truncated = text.substr(0, text.find("AwgnSnrDb"));
    EXPECT_THROW(iq::parse_preset(truncated), std::invalid_argument);
    // Version 1 must not carry version-2 fields or new waveforms.
    EXPECT_THROW(iq::parse_preset(std::string(V1_PRESET) + "NoiseSeed=1\r\n"), std::invalid_argument);
    EXPECT_THROW(iq::parse_preset(std::string(V1_PRESET).replace(std::string(V1_PRESET).find("QPSK"), 4, "WGN")),
                 std::invalid_argument);
    EXPECT_THROW(iq::load_preset("/nonexistent-iq-preset/file"), std::runtime_error);
}
#include <chrono>
TEST(Preset, DiskRoundTripAndWriteFailure) {
    const auto path=std::filesystem::temp_directory_path()/("iq-preset-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    iq::GenerationConfig c; c.pulse=iq::Pulse::Rectangular; c.seed=1234567;
    iq::save_preset(path,c);
    EXPECT_EQ(iq::load_preset(path),c);
    std::filesystem::remove(path);
    EXPECT_THROW(iq::save_preset(path/"missing",c),std::ios_base::failure);
}
