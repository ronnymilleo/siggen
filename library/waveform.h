#pragma once
#include <cstdint>
#include <string_view>
#include <span>

namespace iq {
// Canonical waveform set. WGN is a noise source, not a symbol modulation.
enum class Modulation { BPSK, QPSK, PSK8, QAM16, QAM64, WGN, OOK, PAM4, DBPSK, DQPSK, QAM256, FSK2, FSK4, MSK };
enum class Pulse { RRC, Rectangular };
enum class DataSource { Random, Explicit };
// Linear: mapped symbols convolved with a pulse. Noise: seeded complex WGN.
// Fsk: continuous-phase frequency modulation (2-FSK, 4-FSK, MSK), no pulse filter.
enum class Family { Linear, Noise, Fsk };

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
// Lowercase family identifier used in export and batch metadata.
const char* family_name(Family family);
// Fixed modulation index of MSK (tone spacing / symbol rate).
inline constexpr double MSK_MODULATION_INDEX = 0.5;
// Stable serialized/derivation identifier, independent of enum/table order.
int waveform_id(Modulation modulation);
bool is_valid(Modulation modulation);
// Case-insensitive canonical-name lookup; true and sets out on success.
bool parse_modulation(std::string_view text, Modulation& out);
bool parse_pulse(std::string_view text, Pulse& out);
}
