#include "waveform.h"
#include <gtest/gtest.h>
#include <set>
#include <string>

TEST(Waveform, DescriptorCapabilities) {
    using iq::Modulation;
    const struct { Modulation m; int bps; iq::Family family; bool shaped; bool explicit_input; bool awgn; } expected[] = {
        {Modulation::BPSK, 1, iq::Family::Linear, true, true, true},
        {Modulation::QPSK, 2, iq::Family::Linear, true, true, true},
        {Modulation::PSK8, 3, iq::Family::Linear, true, true, true},
        {Modulation::QAM16, 4, iq::Family::Linear, true, true, true},
        {Modulation::QAM64, 6, iq::Family::Linear, true, true, true},
        {Modulation::WGN, 0, iq::Family::Noise, false, false, false},
    };
    std::set<int> ids;
    std::set<std::string> names;
    for (const auto& entry : expected) {
        const auto& info = iq::waveform_descriptor(entry.m);
        EXPECT_EQ(info.bits_per_symbol, entry.bps);
        EXPECT_EQ(info.family, entry.family);
        EXPECT_EQ(info.shaped, entry.shaped);
        EXPECT_EQ(info.explicit_input, entry.explicit_input);
        EXPECT_EQ(info.awgn_supported, entry.awgn);
        EXPECT_EQ(iq::bits_per_symbol(entry.m), entry.bps);
        EXPECT_EQ(iq::waveform_family(entry.m), entry.family);
        EXPECT_EQ(std::string(iq::modulation_name(entry.m)), std::string(info.canonical_name));
        EXPECT_TRUE(ids.insert(iq::waveform_id(entry.m)).second);
        EXPECT_TRUE(names.insert(std::string(info.canonical_name)).second);
    }
    EXPECT_EQ(iq::waveform_id(Modulation::BPSK), 0);
    EXPECT_EQ(iq::waveform_id(Modulation::WGN), 5);
    EXPECT_FALSE(iq::is_valid(static_cast<Modulation>(99)));
    EXPECT_THROW(iq::waveform_descriptor(static_cast<Modulation>(99)), std::invalid_argument);
}

TEST(Waveform, CaseInsensitiveNameParsing) {
    iq::Modulation m;
    const std::pair<const char*, iq::Modulation> cases[] = {
        {"bpsk", iq::Modulation::BPSK}, {"QPSK", iq::Modulation::QPSK},
        {"8-psk", iq::Modulation::PSK8}, {"8-PSK", iq::Modulation::PSK8},
        {"16-qam", iq::Modulation::QAM16}, {"64-QAM", iq::Modulation::QAM64},
        {"wgn", iq::Modulation::WGN},
    };
    for (const auto& [text, value] : cases) {
        ASSERT_TRUE(iq::parse_modulation(text, m)) << text;
        EXPECT_EQ(m, value);
    }
    EXPECT_FALSE(iq::parse_modulation("GFSK", m));
    EXPECT_FALSE(iq::parse_modulation("", m));
    iq::Pulse pulse;
    EXPECT_TRUE(iq::parse_pulse("rrc", pulse));
    EXPECT_EQ(pulse, iq::Pulse::RRC);
    EXPECT_TRUE(iq::parse_pulse("RRC", pulse));
    EXPECT_TRUE(iq::parse_pulse("rectangular", pulse));
    EXPECT_EQ(pulse, iq::Pulse::Rectangular);
    EXPECT_FALSE(iq::parse_pulse("gaussian", pulse));
}
