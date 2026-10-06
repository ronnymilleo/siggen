#include "waveform.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace iq {
namespace {
constexpr std::array<WaveformDescriptor, 6> descriptors{{
    {Modulation::BPSK, "BPSK", Family::Linear, 1, true, true, true, 0},
    {Modulation::QPSK, "QPSK", Family::Linear, 2, true, true, true, 1},
    {Modulation::PSK8, "8-PSK", Family::Linear, 3, true, true, true, 2},
    {Modulation::QAM16, "16-QAM", Family::Linear, 4, true, true, true, 3},
    {Modulation::QAM64, "64-QAM", Family::Linear, 6, true, true, true, 4},
    {Modulation::WGN, "WGN", Family::Noise, 0, false, false, false, 5},
}};
std::string ascii_lower(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}
}
std::span<const WaveformDescriptor> waveforms() { return descriptors; }
const WaveformDescriptor& waveform_descriptor(Modulation modulation) {
    for (const auto& entry : descriptors)
        if (entry.modulation == modulation) return entry;
    throw std::invalid_argument("Unsupported waveform");
}
Family waveform_family(Modulation modulation) { return waveform_descriptor(modulation).family; }
int bits_per_symbol(Modulation modulation) { return waveform_descriptor(modulation).bits_per_symbol; }
const char* modulation_name(Modulation modulation) { return waveform_descriptor(modulation).canonical_name.data(); }
int waveform_id(Modulation modulation) {
    return waveform_descriptor(modulation).stable_id;
}
bool is_valid(Modulation modulation) {
    return std::any_of(descriptors.begin(), descriptors.end(),
                       [&](const auto& entry) { return entry.modulation == modulation; });
}
bool parse_modulation(std::string_view text, Modulation& out) {
    const auto lowered = ascii_lower(text);
    for (const auto& entry : descriptors)
        if (ascii_lower(entry.canonical_name) == lowered) { out = entry.modulation; return true; }
    return false;
}
bool parse_pulse(std::string_view text, Pulse& out) {
    const auto lowered = ascii_lower(text);
    if (lowered == "rrc" || lowered == "root-raised cosine") { out = Pulse::RRC; return true; }
    if (lowered == "rectangular" || lowered == "rect") { out = Pulse::Rectangular; return true; }
    return false;
}
}
