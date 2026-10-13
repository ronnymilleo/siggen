/**
 * @file    modulation_test_helpers.h
 * @brief   Constellation helpers shared by the modulation tests.
 */

#ifndef SIGGEN_MODULATION_TEST_HELPERS_H
#define SIGGEN_MODULATION_TEST_HELPERS_H

#include "generator.h"
#include "test_support.h"
#include <complex>
#include <string>
#include <vector>

namespace Core {

using Sym = std::complex<float>;

// Bit label of value, most significant bit first
inline std::string Label(unsigned value, int bits) {
    std::string text;
    for (int k = bits - 1; k >= 0; --k) {
        text += ((value >> k) & 1u) ? '1' : '0';
    }
    return text;
}

// Full constellation of a memoryless mapper, one point per bit label
inline std::vector<Sym> Constellation(Modulation modulation) {
    const int bits_per_symbol = BitsPerSymbol(modulation);
    std::vector<Sym> points;
    for (unsigned v = 0; v < (1u << bits_per_symbol); ++v) {
        points.push_back(Unwrap(MapSymbols(modulation, Label(v, bits_per_symbol)))[0]);
    }
    return points;
}

} // namespace Core

#endif // SIGGEN_MODULATION_TEST_HELPERS_H
