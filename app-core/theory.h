/**
 * @file    theory.h
 * @brief   Textbook bit error probabilities over AWGN with ideal receivers.
 */

#ifndef SIGGEN_THEORY_H
#define SIGGEN_THEORY_H

#include "waveform.h"
#include <optional>

namespace Core {

std::optional<double> TheoreticalBer(Modulation modulation, double eb_n0_db);
double QFunction(double x);

} // namespace Core

#endif // SIGGEN_THEORY_H
