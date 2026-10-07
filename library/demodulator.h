#pragma once
#include "generator.h"
#include <cstddef>
#include <limits>
#include <optional>
namespace iq {
// Hard-decision reference receiver for the linear waveforms. It is an *ideal* receiver on purpose, so
// the numbers isolate the channel: perfect symbol timing, known amplitude gain, the matched filter
// of the transmit pulse, and no carrier or phase recovery. Coherent schemes therefore fail under a
// carrier offset, while the differential ones (DBPSK, DQPSK, pi/4-DQPSK, 8-DPSK) decide on the phase
// step between consecutive observations and keep working until the offset eats the decision margin.
bool is_differential(Modulation modulation);
// True for the linear waveforms (not WGN, FSK or MSK).
bool has_reference_demodulator(Modulation modulation);

struct BitErrors {
    std::size_t symbol_count = 0;
    std::size_t symbol_errors = 0;
    std::size_t bit_count = 0;
    std::size_t bit_errors = 0;
    double ber() const { return bit_count ? static_cast<double>(bit_errors) / static_cast<double>(bit_count) : 0.0; }
    double ser() const { return symbol_count ? static_cast<double>(symbol_errors) / static_cast<double>(symbol_count) : 0.0; }
    BitErrors& operator+=(const BitErrors& other);
};
// Demodulates the steady-state symbols (one RRC span excluded at each edge, as for EVM) and compares
// them with the transmitted bits. Differential schemes skip the first symbol of the range, which has
// no previous observation. [first_symbol, end_symbol) restricts the symbols scored. Empty when the
// waveform is not linear, the signal carries no bits, or no symbol qualifies.
std::optional<BitErrors> bit_errors(const GeneratedSignal& signal, std::size_t first_symbol = 0,
                                    std::size_t end_symbol = std::numeric_limits<std::size_t>::max());
}
