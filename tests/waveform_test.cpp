/**
 * @file    waveform_test.cpp
 * @brief   Tests for waveform descriptors, stable identifiers and case-insensitive name parsing.
 */

#include "waveform.h"

#include <gtest/gtest.h>
#include <set>
#include <string>

namespace Core {

TEST(Waveform, DescriptorCapabilities) {
    const struct {
        Modulation Waveform;
        int BitCount;
        Family ExpectedFamily;
        bool Shaped;
        bool ExplicitInput;
        bool Awgn;
    } expected[] = {
        {Modulation::BPSK, 1, Family::Linear, true, true, true},
        {Modulation::QPSK, 2, Family::Linear, true, true, true},
        {Modulation::PSK8, 3, Family::Linear, true, true, true},
        {Modulation::QAM16, 4, Family::Linear, true, true, true},
        {Modulation::QAM64, 6, Family::Linear, true, true, true},
        {Modulation::WGN, 0, Family::Noise, false, false, false},
    };
    std::set<int> ids;
    std::set<std::string> names;
    for (const auto &entry : expected) {
        const auto &info = GetWaveformDescriptor(entry.Waveform);
        EXPECT_EQ(info.BitsPerSymbol, entry.BitCount);
        EXPECT_EQ(info.Family, entry.ExpectedFamily);
        EXPECT_EQ(info.Shaped, entry.Shaped);
        EXPECT_EQ(info.ExplicitInput, entry.ExplicitInput);
        EXPECT_EQ(info.AwgnSupported, entry.Awgn);
        EXPECT_EQ(BitsPerSymbol(entry.Waveform), entry.BitCount);
        EXPECT_EQ(WaveformFamily(entry.Waveform), entry.ExpectedFamily);
        EXPECT_EQ(std::string(ModulationName(entry.Waveform)), std::string(info.CanonicalName));
        EXPECT_TRUE(ids.insert(WaveformId(entry.Waveform)).second);
        EXPECT_TRUE(names.insert(std::string(info.CanonicalName)).second);
    }
    EXPECT_EQ(WaveformId(Modulation::BPSK), 0);
    EXPECT_EQ(WaveformId(Modulation::WGN), 5);
    EXPECT_FALSE(IsValid(static_cast<Modulation>(99)));
    EXPECT_THROW(GetWaveformDescriptor(static_cast<Modulation>(99)), std::invalid_argument);
}

TEST(Waveform, CaseInsensitiveNameParsing) {
    Modulation parsed;
    const std::pair<const char *, Modulation> cases[] = {
        {"bpsk", Modulation::BPSK},  {"QPSK", Modulation::QPSK},    {"8-psk", Modulation::PSK8},
        {"8-PSK", Modulation::PSK8}, {"16-qam", Modulation::QAM16}, {"64-QAM", Modulation::QAM64},
        {"wgn", Modulation::WGN},
    };
    for (const auto &[text, value] : cases) {
        ASSERT_TRUE(ParseModulation(text, parsed)) << text;
        EXPECT_EQ(parsed, value);
    }
    EXPECT_FALSE(ParseModulation("GFSK", parsed));
    EXPECT_FALSE(ParseModulation("", parsed));
    Pulse pulse;
    EXPECT_TRUE(ParsePulse("rrc", pulse));
    EXPECT_EQ(pulse, Pulse::RRC);
    EXPECT_TRUE(ParsePulse("RRC", pulse));
    EXPECT_TRUE(ParsePulse("rectangular", pulse));
    EXPECT_EQ(pulse, Pulse::Rectangular);
    EXPECT_FALSE(ParsePulse("gaussian", pulse));
}

} // namespace Core
