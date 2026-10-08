/**
 * @file    demodulator.cpp
 * @brief   Hard-decision reference receiver for the linear waveforms, and the bit errors it counts.
 */

#include "demodulator.h"

#include "signal_analysis.h"
#include <bit>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <string>
#include <vector>

namespace Core {

namespace {

// The bits of a symbol label, most significant first
std::string LabelBits(int label, int bits_per_symbol) {
    std::string bits(static_cast<std::size_t>(bits_per_symbol), '0');
    for (int k = 0; k < bits_per_symbol; ++k) {
        bits[static_cast<std::size_t>(k)] = ((label >> (bits_per_symbol - 1 - k)) & 1) ? '1' : '0';
    }
    return bits;
}

int LabelOf(const std::string &bits, std::size_t offset, int bits_per_symbol) {
    int label = 0;
    for (int k = 0; k < bits_per_symbol; ++k) {
        label = (label << 1) | (bits[offset + static_cast<std::size_t>(k)] == '1' ? 1 : 0);
    }
    return label;
}

// Ideal decision points (coherent) or ideal phase steps (differential), taken from the mapper itself
std::vector<std::complex<double>> ReferencePoints(const GenerationConfig &config, bool differential,
                                                  int bits_per_symbol) {
    const int label_count = 1 << bits_per_symbol;
    std::vector<std::complex<double>> reference(static_cast<std::size_t>(label_count));
    for (int label = 0; label < label_count; ++label) {
        if (!differential) {
            reference[static_cast<std::size_t>(label)] =
                std::complex<double>(MapSymbols(config.Modulation, LabelBits(label, bits_per_symbol))[0]) *
                config.AmplitudeGain;
        } else {
            // Symbol after label 0, then after label `label`: the ratio is the phase step of `label`
            const auto pair =
                MapSymbols(config.Modulation, LabelBits(0, bits_per_symbol) + LabelBits(label, bits_per_symbol));
            reference[static_cast<std::size_t>(label)] = std::complex<double>(pair[1]) / std::complex<double>(pair[0]);
        }
    }
    return reference;
}

int NearestPoint(std::complex<double> value, const std::vector<std::complex<double>> &reference) {
    int decided = 0;
    double best_distance = -1;
    for (std::size_t label = 0; label < reference.size(); ++label) {
        const double distance = std::norm(value - reference[label]);
        if (best_distance < 0 || distance < best_distance) {
            best_distance = distance;
            decided = static_cast<int>(label);
        }
    }
    return decided;
}

int ClosestPhaseStep(std::complex<double> step, const std::vector<std::complex<double>> &reference) {
    int decided = 0;
    double best_match = -2;
    for (std::size_t label = 0; label < reference.size(); ++label) {
        const double match = std::real(step * std::conj(reference[label]));
        if (match > best_match) {
            best_match = match;
            decided = static_cast<int>(label);
        }
    }
    return decided;
}

} // namespace

/**
 * @brief   Bit error rate of the counted bits.
 * @return  Bit errors over bits compared; zero when no bit was compared.
 */
double BitErrors::Ber() const {
    return BitCount ? static_cast<double>(BitErrorCount) / static_cast<double>(BitCount) : 0.0;
}

/**
 * @brief   Symbol error rate of the counted symbols.
 * @return  Symbol errors over symbols compared; zero when no symbol was compared.
 */
double BitErrors::Ser() const {
    return SymbolCount ? static_cast<double>(SymbolErrorCount) / static_cast<double>(SymbolCount) : 0.0;
}

/**
 * @brief   Accumulates the counts of another measurement.
 * @param[in] other  Counts to add.
 * @return  This object.
 */
BitErrors &BitErrors::operator+=(const BitErrors &other) {
    SymbolCount += other.SymbolCount;
    SymbolErrorCount += other.SymbolErrorCount;
    BitCount += other.BitCount;
    BitErrorCount += other.BitErrorCount;
    return *this;
}

/**
 * @brief   Tells whether the reference receiver decides on phase steps instead of absolute points.
 * @param[in] modulation  Waveform.
 * @return  True for DBPSK, DQPSK, pi/4-DQPSK and 8-DPSK.
 */
bool IsDifferential(Modulation modulation) {
    return modulation == Modulation::DBPSK || modulation == Modulation::DQPSK || modulation == Modulation::PI4DQPSK ||
           modulation == Modulation::DPSK8;
}

/**
 * @brief   Tells whether the reference receiver can demodulate a waveform.
 * @param[in] modulation  Waveform.
 * @return  True for the linear waveforms (not WGN, FSK or MSK).
 */
bool HasReferenceDemodulator(Modulation modulation) {
    return IsValid(modulation) && WaveformFamily(modulation) == Family::Linear;
}

/**
 * @brief   Demodulates the steady-state symbols and compares them with the transmitted bits.
 * @param[in] signal        Generated (or rebuilt) signal with its transmitted bits.
 * @param[in] first_symbol  First symbol index scored.
 * @param[in] end_symbol    One past the last symbol index scored.
 * @return  The symbol and bit error counts. Empty when the waveform is not linear, the signal carries no bits, or
 *          no symbol qualifies.
 * @note    One RRC span is excluded at each edge, as for EVM. Differential schemes skip the first symbol of the
 *          range, which has no previous observation. Throws std::invalid_argument when the bits do not match the
 *          symbols.
 */
std::optional<BitErrors> CountBitErrors(const GeneratedSignal &signal, std::size_t first_symbol,
                                        std::size_t end_symbol) {
    const auto modulation = signal.Config.Modulation;
    if (signal.Family != Family::Linear || !HasReferenceDemodulator(modulation) || signal.Bits.empty()) {
        return std::nullopt;
    }
    const int bits_per_symbol = BitsPerSymbol(modulation);
    const auto observations = MatchedSymbols(signal);
    if (signal.Bits.size() != signal.Symbols.size() * static_cast<std::size_t>(bits_per_symbol)) {
        throw std::invalid_argument("Signal bits do not match its symbols");
    }
    const bool differential = IsDifferential(modulation);
    const auto reference = ReferencePoints(signal.Config, differential, bits_per_symbol);

    BitErrors result;
    for (std::size_t i = 0; i < observations.Values.size(); ++i) {
        const auto index = observations.SymbolIndices[i];
        if (index < first_symbol || index >= end_symbol) {
            continue;
        }
        int decided = 0;
        if (!differential) {
            decided = NearestPoint(std::complex<double>(observations.Values[i]), reference);
        } else {
            if (i == 0 || observations.SymbolIndices[i - 1] + 1 != index ||
                observations.SymbolIndices[i - 1] < first_symbol) {
                continue;
            }
            const auto step = std::complex<double>(observations.Values[i]) *
                              std::conj(std::complex<double>(observations.Values[i - 1]));
            decided = ClosestPhaseStep(step, reference);
        }
        const int sent = LabelOf(signal.Bits, index * static_cast<std::size_t>(bits_per_symbol), bits_per_symbol);
        ++result.SymbolCount;
        result.BitCount += static_cast<std::size_t>(bits_per_symbol);
        if (decided != sent) {
            ++result.SymbolErrorCount;
            result.BitErrorCount += static_cast<std::size_t>(std::popcount(static_cast<unsigned>(decided ^ sent)));
        }
    }
    if (result.SymbolCount == 0) {
        return std::nullopt;
    }
    return result;
}

} // namespace Core
