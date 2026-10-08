/**
 * @file    theory.cpp
 * @brief   Textbook bit error probabilities over AWGN with ideal receivers.
 */

#include "theory.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Core {

namespace {

// exp(-x) * I0(x) for x >= 0 (Abramowitz and Stegun 9.8.1 and 9.8.2)
double ScaledBesselI0(double x) {
    if (x < 3.75) {
        const double t = (x / 3.75) * (x / 3.75);
        const double i0 =
            1 + t * (3.5156229 + t * (3.0899424 + t * (1.2067492 + t * (0.2659732 + t * (0.0360768 + t * 0.0045813)))));
        return i0 * std::exp(-x);
    }
    const double t = 3.75 / x;
    const double poly =
        0.39894228 +
        t * (0.01328592 +
             t * (0.00225319 +
                  t * (-0.00157565 +
                       t * (0.00916281 + t * (-0.02057706 + t * (0.02635537 + t * (-0.01647633 + t * 0.00392377)))))));
    return poly / std::sqrt(x);
}

// Marcum Q1(a, b) = integral from b to infinity of x exp(-(x^2 + a^2) / 2) I0(a x) dx, by Simpson's rule
double MarcumQ1(double a, double b) {
    const double upper = std::max(a, b) + 12;
    if (upper <= b) {
        return 0;
    }
    constexpr int Steps = 4000; // Simpson's rule needs an even count
    const double step = (upper - b) / Steps;
    const auto integrand = [&](double x) { return x * std::exp(-0.5 * (x - a) * (x - a)) * ScaledBesselI0(a * x); };
    double sum = integrand(b) + integrand(upper);
    for (int k = 1; k < Steps; ++k) {
        sum += integrand(b + k * step) * (k % 2 ? 4 : 2);
    }
    return sum * step / 3;
}

// Gray-coded M-PSK, M >= 4
double PskBer(int bits, double gamma) {
    const double order = std::pow(2.0, bits);
    return 2.0 / bits * QFunction(std::sqrt(2.0 * bits * gamma) * std::sin(std::numbers::pi / order));
}

double SquareQamBer(int bits, double gamma) {
    const double order = std::pow(2.0, bits);
    return 4.0 / bits * (1 - 1 / std::sqrt(order)) * QFunction(std::sqrt(3.0 * bits * gamma / (order - 1)));
}

// Differentially coherent Gray QPSK (Proakis, 8.6): Q1(a, b) - 1/2 I0(ab) exp(-(a^2 + b^2) / 2)
double DifferentialQpskBer(double gamma) {
    const double root = std::sqrt(2 * gamma);
    const double a = root * std::sqrt(1 - 1 / std::numbers::sqrt2);
    const double b = root * std::sqrt(1 + 1 / std::numbers::sqrt2);
    return MarcumQ1(a, b) - 0.5 * ScaledBesselI0(a * b) * std::exp(-0.5 * (b - a) * (b - a));
}

} // namespace

/**
 * @brief   Textbook bit error probability over AWGN with an ideal receiver.
 * @param[in] modulation  Waveform.
 * @param[in] eb_n0_db    Eb/N0 (average energy per bit), in dB.
 * @return  The bit error probability. Empty where no standard closed form is used (8-DPSK, 32-QAM cross, FSK,
 *          MSK, WGN).
 * @note    Coherent PSK/QAM/PAM use the Gray-coded nearest-neighbour approximation (exact for BPSK, QPSK and
 *          OQPSK); DBPSK is exact; DQPSK and pi/4-DQPSK use the exact differentially coherent expression.
 */
std::optional<double> TheoreticalBer(Modulation modulation, double eb_n0_db) {
    const double gamma = std::pow(10.0, eb_n0_db / 10);
    switch (modulation) {
    case Modulation::BPSK:
    case Modulation::QPSK:
    case Modulation::OQPSK:
        return QFunction(std::sqrt(2 * gamma));
    case Modulation::PSK8:
        return PskBer(3, gamma);
    case Modulation::QAM16:
        return SquareQamBer(4, gamma);
    case Modulation::QAM64:
        return SquareQamBer(6, gamma);
    case Modulation::QAM256:
        return SquareQamBer(8, gamma);
    case Modulation::PAM4:
        return 2.0 * (1 - 1.0 / 4) / 2 * QFunction(std::sqrt(6.0 * 2 * gamma / (16 - 1)));
    case Modulation::OOK:
        return QFunction(std::sqrt(gamma));
    case Modulation::ASK4:
        return 0.5 * 1.5 * QFunction(std::sqrt(2 * gamma / 7)); // Unipolar levels 0..3, Gray labels
    case Modulation::DBPSK:
        return 0.5 * std::exp(-gamma);
    case Modulation::DQPSK:
    case Modulation::PI4DQPSK:
        return DifferentialQpskBer(gamma);
    default:
        return std::nullopt;
    }
}

/**
 * @brief   Gaussian tail probability Q(x).
 * @param[in] x  Argument.
 * @return  The probability that a standard normal variable exceeds x.
 */
double QFunction(double x) {
    return 0.5 * std::erfc(x / std::numbers::sqrt2);
}

} // namespace Core
