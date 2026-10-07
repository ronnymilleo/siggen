/**
 * @file    preset_test.cpp
 * @brief   Tests for preset serialization, legacy versions, malformed input and disk round trips.
 */

#include "preset.h"

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>

namespace Core {

namespace {

const char *const V1Preset =
    "[IQ Generator Preset]\r\nVersion=1\r\nModulation=QPSK\r\nNumberOfSymbols=32\r\nSymbolRateBaud=1000\r\n"
    "SamplesPerSymbol=8\r\nPulse=RRC\r\nRRCBeta=0.2\r\nSpanSymbols=10\r\nAmplitudeGain=1\r\nDataSource=Random\r\n"
    "Seed=5489\r\nBits=\r\n";

} // namespace

TEST(Preset, RoundTripAndLegacyDefaults) {
    for (auto modulation :
         {Modulation::BPSK, Modulation::QPSK, Modulation::PSK8, Modulation::QAM16, Modulation::QAM64}) {
        GenerationConfig config;
        config.Modulation = modulation;
        config.DataSource = DataSource::Explicit;
        config.Bits = std::string(static_cast<std::size_t>(config.SymbolCount) * BitsPerSymbol(modulation), '1');
        config.AmplitudeGain = 1.234567890123;
        config.Seed = 4294967295u;
        config.Awgn.Enabled = true;
        config.Awgn.SnrDb = -7.5;
        config.NoiseSeed = 99;
        EXPECT_EQ(ParsePreset(SerializePreset(config)), config);
    }
    GenerationConfig wgn;
    wgn.Modulation = Modulation::WGN;
    wgn.NoiseSource.SampleCount = 4096;
    wgn.NoiseSource.SampleRateHz = 12345.6789;
    wgn.NoiseSource.NoisePower = 2.5;
    wgn.NoiseSeed = 5490;
    EXPECT_EQ(ParsePreset(SerializePreset(wgn)), wgn);
    EXPECT_NE(SerializePreset(wgn).find("Version=2"), std::string::npos);
    auto legacy =
        ParsePreset("[SignalGenerator "
                    "Preset]\r\nNumberOfSymbols=32\r\nSamplesPerSymbol=4\r\nRRCBeta=0.3\r\nConstellationStart=43\r\n");
    EXPECT_EQ(legacy.SymbolCount, 32);
    EXPECT_EQ(legacy.SamplesPerSymbol, 4);
    EXPECT_EQ(legacy.SymbolRateBaud, 1000);
    EXPECT_EQ(legacy.Seed, 5489u);
    EXPECT_EQ(legacy.SpanSymbols, 10);
    // Version-1 imports receive noise-disabled defaults.
    auto v1 = ParsePreset(V1Preset);
    EXPECT_EQ(v1.Modulation, Modulation::QPSK);
    EXPECT_EQ(v1.SymbolCount, 32);
    EXPECT_FALSE(v1.Awgn.Enabled);
    EXPECT_EQ(v1.NoiseSeed, 5490u);
    EXPECT_EQ(v1.NoiseSource, NoiseSourceSettings{});
}

TEST(Preset, MalformedAndTransactionalApplication) {
    GenerationConfig current;
    current.Seed = 123;
    const auto before = current;
    const auto text = SerializePreset({});
    for (const auto &change :
         std::vector<std::pair<std::string, std::string>>{{"Version=2", "Version=3"},
                                                          {"NumberOfSymbols=256", "NumberOfSymbols=2oops"},
                                                          {"RRCBeta=", "RRCBeta=nan"},
                                                          {"Seed=5489", "Seed=-1"},
                                                          {"SamplesPerSymbol=8", "SamplesPerSymbol=0"},
                                                          {"Pulse=RRC", "Pulse=Unknown"},
                                                          {"Bits=", "Bits=x"},
                                                          {"DataSource=Random", "DataSource=Explicit"},
                                                          {"AwgnEnabled=false", "AwgnEnabled=maybe"},
                                                          {"NoiseSeed=5490", "NoiseSeed=-1"},
                                                          {"NoiseSampleCount=2048", "NoiseSampleCount=0"},
                                                          {"NoisePower=1", "NoisePower=inf"},
                                                          {"Modulation=BPSK", "Modulation=GFSK"}}) {
        auto bad = text;
        bad.replace(bad.find(change.first), change.first.size(), change.second);
        EXPECT_THROW(current = ParsePreset(bad), std::invalid_argument) << change.first;
        EXPECT_EQ(current, before);
    }
    EXPECT_THROW(ParsePreset(text + "Seed=5\n"), std::invalid_argument);
    EXPECT_THROW(ParsePreset(text + "Unknown=1\n"), std::invalid_argument);
    EXPECT_THROW(ParsePreset(""), std::invalid_argument);
    EXPECT_THROW(ParsePreset("[IQ Generator Preset]\nVersion=2\n"), std::invalid_argument);
    // Missing noise fields are rejected in version 2.
    auto truncated = text.substr(0, text.find("AwgnSnrDb"));
    EXPECT_THROW(ParsePreset(truncated), std::invalid_argument);
    // Version 1 must not carry version-2 fields or new waveforms.
    EXPECT_THROW(ParsePreset(std::string(V1Preset) + "NoiseSeed=1\r\n"), std::invalid_argument);
    EXPECT_THROW(ParsePreset(std::string(V1Preset).replace(std::string(V1Preset).find("QPSK"), 4, "WGN")),
                 std::invalid_argument);
    EXPECT_THROW(LoadPreset("/nonexistent-iq-preset/file"), std::runtime_error);
}

TEST(Preset, DiskRoundTripAndWriteFailure) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("iq-preset-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    GenerationConfig config;
    config.Pulse = Pulse::Rectangular;
    config.Seed = 1234567;
    SavePreset(path, config);
    EXPECT_EQ(LoadPreset(path), config);
    std::filesystem::remove(path);
    EXPECT_THROW(SavePreset(path / "missing", config), std::ios_base::failure);
}

} // namespace Core
