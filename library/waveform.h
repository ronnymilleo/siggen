#pragma once
#include <cstdint>
#include <string_view>
#include <span>

namespace iq {
// Canonical waveform set. WGN is a noise source, not a symbol modulation.
enum class Modulation { BPSK, QPSK, PSK8, QAM16, QAM64, WGN };
enum class Pulse { RRC, Rectangular };
enum class DataSource { Random, Explicit };
enum class Family { Linear, Noise };

// Shared capability table consumed by CLI, GUI, validation, serialization and analysis.
struct WaveformDescriptor {
    Modulation modulation;
    std::string_view canonical_name; // Case-insensitive CLI/preset identifier.
    Family family;
    int bits_per_symbol;             // Zero for noise sources.
    bool shaped;                     // Pulse shaping applies (linear families).
    bool explicit_input;             // Exact-count explicit bits supported.
    bool awgn_supported;             // Optional AWGN overlay applies.
    int stable_id;                   // Dataset seed ID; never renumber existing entries.
};

std::span<const WaveformDescriptor> waveforms();
const WaveformDescriptor& waveform_descriptor(Modulation modulation);
Family waveform_family(Modulation modulation);
int bits_per_symbol(Modulation modulation);
const char* modulation_name(Modulation modulation);
// Stable serialized/derivation identifier, independent of enum/table order.
int waveform_id(Modulation modulation);
bool is_valid(Modulation modulation);
// Case-insensitive canonical-name lookup; true and sets out on success.
bool parse_modulation(std::string_view text, Modulation& out);
bool parse_pulse(std::string_view text, Pulse& out);
}
