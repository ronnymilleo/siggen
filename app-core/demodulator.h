/**
 * @file    demodulator.h
 * @brief   Hard-decision reference receiver for the linear waveforms, and the bit errors it counts.
 * @details It is an *ideal* receiver on purpose, so the numbers isolate the channel: perfect symbol timing, known
 *          amplitude gain, the matched filter of the transmit pulse, and no carrier or phase recovery. Coherent
 *          schemes therefore fail under a carrier offset, while the differential ones (DBPSK, DQPSK, pi/4-DQPSK,
 *          8-DPSK) decide on the phase step between consecutive observations and keep working until the offset
 *          eats the decision margin.
 */

#ifndef SIGGEN_DEMODULATOR_H
#define SIGGEN_DEMODULATOR_H

#include "generator.h"
#include <cstddef>
#include <limits>
#include <optional>

namespace Core {

/**
 * @struct  BitErrors
 * @brief   Symbol and bit error counts of the reference receiver against the transmitted bits.
 */
struct BitErrors {
    std::size_t SymbolCount = 0;
    std::size_t SymbolErrorCount = 0;
    std::size_t BitCount = 0;
    std::size_t BitErrorCount = 0;

    double Ber() const;
    double Ser() const;

    BitErrors &operator+=(const BitErrors &other);
};

bool IsDifferential(Modulation modulation);
bool HasReferenceDemodulator(Modulation modulation);
std::optional<BitErrors> CountBitErrors(const GeneratedSignal &signal, std::size_t first_symbol = 0,
                                        std::size_t end_symbol = std::numeric_limits<std::size_t>::max());

} // namespace Core

#endif // SIGGEN_DEMODULATOR_H
