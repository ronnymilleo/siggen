/**
 * @file    waveform.cpp
 * @brief   The waveforms the generator supports and the capability table that describes each of them.
 */

#include "waveform.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <span>
#include <string>
#include <string_view>

namespace Core {

namespace {

constexpr std::array<WaveformDescriptor, 19> Descriptors{{
    {Modulation::BPSK, "BPSK", Family::Linear, 1, true, true, true, 0},
    {Modulation::QPSK, "QPSK", Family::Linear, 2, true, true, true, 1},
    {Modulation::PSK8, "8-PSK", Family::Linear, 3, true, true, true, 2},
    {Modulation::QAM16, "16-QAM", Family::Linear, 4, true, true, true, 3},
    {Modulation::QAM64, "64-QAM", Family::Linear, 6, true, true, true, 4},
    {Modulation::WGN, "WGN", Family::Noise, 0, false, false, false, 5},
    {Modulation::OOK, "OOK", Family::Linear, 1, true, true, true, 6},
    {Modulation::PAM4, "4-PAM", Family::Linear, 2, true, true, true, 7},
    {Modulation::DBPSK, "DBPSK", Family::Linear, 1, true, true, true, 8},
    {Modulation::DQPSK, "DQPSK", Family::Linear, 2, true, true, true, 9},
    {Modulation::QAM256, "256-QAM", Family::Linear, 8, true, true, true, 10},
    {Modulation::FSK2, "2-FSK", Family::Fsk, 1, false, true, true, 11},
    {Modulation::FSK4, "4-FSK", Family::Fsk, 2, false, true, true, 12},
    {Modulation::MSK, "MSK", Family::Fsk, 1, false, true, true, 13},
    {Modulation::QAM32, "32-QAM", Family::Linear, 5, true, true, true, 14},
    {Modulation::OQPSK, "OQPSK", Family::Linear, 2, true, true, true, 15},
    {Modulation::PI4DQPSK, "pi/4-DQPSK", Family::Linear, 2, true, true, true, 16},
    {Modulation::DPSK8, "8-DPSK", Family::Linear, 3, true, true, true, 17},
    {Modulation::ASK4, "4-ASK", Family::Linear, 2, true, true, true, 18},
}};

std::string AsciiLower(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

} // namespace

/**
 * @brief   Returns the capability table.
 * @return  One descriptor per supported waveform.
 */
std::span<const WaveformDescriptor> Waveforms() {
    return Descriptors;
}

/**
 * @brief   Looks up the capabilities of a waveform.
 * @param[in] modulation  Waveform to look up.
 * @return  Its descriptor, or nullptr for a value outside the table.
 */
const WaveformDescriptor *FindWaveform(const Modulation modulation) {
    for (const auto &entry : Descriptors) {
        if (entry.Modulation == modulation) {
            return &entry;
        }
    }
    return nullptr;
}

/**
 * @brief   Returns how a waveform is generated.
 * @param[in] modulation  Waveform to look up.
 * @return  Its family; Family::Linear for an unsupported value, which validated paths never produce.
 */
Family WaveformFamily(const Modulation modulation) {
    const auto *descriptor = FindWaveform(modulation);
    return descriptor ? descriptor->Family : Family::Linear;
}

/**
 * @brief   Returns the lowercase family identifier used in export and batch metadata.
 * @param[in] family  Family to name.
 * @return  "linear", "noise" or "fsk".
 */
const char *FamilyName(Family family) {
    switch (family) {
    case Family::Linear:
        return "linear";
    case Family::Noise:
        return "noise";
    case Family::Fsk:
        return "fsk";
    }
    return "linear";
}

/**
 * @brief   Returns the number of bits each symbol of a waveform carries.
 * @param[in] modulation  Waveform to look up.
 * @return  Bits per symbol; zero for noise sources and unsupported values.
 */
int BitsPerSymbol(const Modulation modulation) {
    const auto *descriptor = FindWaveform(modulation);
    return descriptor ? descriptor->BitsPerSymbol : 0;
}

/**
 * @brief   Returns the canonical name of a waveform, as used by the CLI and presets.
 * @param[in] modulation  Waveform to name.
 * @return  The canonical name, such as "16-QAM"; "unknown" for an unsupported value.
 */
const char *ModulationName(const Modulation modulation) {
    const auto *descriptor = FindWaveform(modulation);
    return descriptor ? descriptor->CanonicalName.data() : "unknown";
}

/**
 * @brief   Returns the stable serialized and seed-derivation identifier of a waveform.
 * @param[in] modulation  Waveform to look up.
 * @return  An identifier independent of the enum and table order; -1 for an unsupported value.
 */
int WaveformId(const Modulation modulation) {
    const auto *descriptor = FindWaveform(modulation);
    return descriptor ? descriptor->StableId : -1;
}

/**
 * @brief   Checks whether a value names a supported waveform.
 * @param[in] modulation  Value to check, possibly read from untrusted input.
 * @return  True when the capability table has an entry for it.
 */
bool IsValid(Modulation modulation) {
    return FindWaveform(modulation) != nullptr;
}

/**
 * @brief   Finds a waveform by its canonical name, ignoring ASCII case.
 * @param[in]  text  Name to look up.
 * @param[out] out   Set to the waveform on success; left unchanged otherwise.
 * @return  True when the name matched.
 */
bool ParseModulation(std::string_view text, Modulation &out) {
    const auto lowered = AsciiLower(text);
    for (const auto &entry : Descriptors) {
        if (AsciiLower(entry.CanonicalName) == lowered) {
            out = entry.Modulation;
            return true;
        }
    }
    return false;
}

/**
 * @brief   Reads a pulse shape name, ignoring ASCII case.
 * @param[in]  text  "rrc", "root-raised cosine", "rectangular" or "rect".
 * @param[out] out   Set to the pulse on success; left unchanged otherwise.
 * @return  True when the name matched.
 */
bool ParsePulse(std::string_view text, Pulse &out) {
    const auto lowered = AsciiLower(text);
    if (lowered == "rrc" || lowered == "root-raised cosine") {
        out = Pulse::RRC;
        return true;
    }
    if (lowered == "rectangular" || lowered == "rect") {
        out = Pulse::Rectangular;
        return true;
    }
    return false;
}

} // namespace Core
