/**
 * @file    waveform.h
 * @brief   The waveforms the generator supports and the capability table that describes each of them.
 */

#ifndef SIGGEN_WAVEFORM_H
#define SIGGEN_WAVEFORM_H

#include <cstdint>
#include <span>
#include <string_view>

namespace Core {

/**
 * @enum    Modulation
 * @brief   Canonical waveform set.
 * @details WGN is a noise source, not a symbol modulation.
 */
enum class Modulation {
    BPSK,
    QPSK,
    PSK8,
    QAM16,
    QAM64,
    WGN,
    OOK,
    PAM4,
    DBPSK,
    DQPSK,
    QAM256,
    FSK2,
    FSK4,
    MSK,
    QAM32,
    OQPSK,
    PI4DQPSK,
    DPSK8,
    ASK4
};

/**
 * @enum    Pulse
 * @brief   Pulse shape that linear families convolve their symbols with.
 */
enum class Pulse {
    RRC,
    Rectangular
};

/**
 * @enum    DataSource
 * @brief   Where the transmitted bits come from: a seeded random generator or bits given by the user.
 */
enum class DataSource {
    Random,
    Explicit
};

/**
 * @enum    Family
 * @brief   How a waveform is generated.
 * @details Linear: mapped symbols convolved with a pulse. Noise: seeded complex WGN. Fsk: continuous-phase
 *          frequency modulation (2-FSK, 4-FSK, MSK), no pulse filter.
 */
enum class Family {
    Linear,
    Noise,
    Fsk
};

/**
 * @struct  WaveformDescriptor
 * @brief   Capabilities of one waveform, shared by the CLI, GUI, validation, serialization and analysis.
 */
struct WaveformDescriptor {
    Core::Modulation Modulation;
    std::string_view CanonicalName; // Case-insensitive CLI/preset identifier
    Core::Family Family;
    int BitsPerSymbol;  // Zero for noise sources
    bool Shaped;        // Pulse shaping applies (linear families)
    bool ExplicitInput; // Exact-count explicit bits supported
    bool AwgnSupported; // Optional AWGN overlay applies
    int StableId;       // Dataset seed ID; never renumber existing entries
};

// Fixed modulation index of MSK (tone spacing / symbol rate)
inline constexpr double MskModulationIndex = 0.5;

// Capability table
std::span<const WaveformDescriptor> Waveforms();
const WaveformDescriptor &GetWaveformDescriptor(Modulation modulation);
Family WaveformFamily(Modulation modulation);
const char *FamilyName(Family family);
int BitsPerSymbol(Modulation modulation);
const char *ModulationName(Modulation modulation);
int WaveformId(Modulation modulation);
bool IsValid(Modulation modulation);

// Parsing
bool ParseModulation(std::string_view text, Modulation &out);
bool ParsePulse(std::string_view text, Pulse &out);

} // namespace Core

#endif // SIGGEN_WAVEFORM_H
