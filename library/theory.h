#pragma once
#include "waveform.h"
#include <optional>
namespace iq {
// Textbook bit error probability over AWGN with ideal receivers, against Eb/N0 (average energy per bit).
// Coherent PSK/QAM/PAM use the Gray-coded nearest-neighbour approximation (exact for BPSK, QPSK and
// OQPSK); DBPSK is exact; DQPSK and pi/4-DQPSK use the exact differentially coherent expression.
// Empty where no standard closed form is used (8-DPSK, 32-QAM cross, FSK, MSK, WGN).
std::optional<double> theoretical_ber(Modulation modulation, double eb_n0_db);
// Gaussian tail probability Q(x).
double q_function(double x);
}
