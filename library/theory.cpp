#include "theory.h"
#include <cmath>
#include <numbers>
namespace iq {
double q_function(double x) { return 0.5 * std::erfc(x / std::numbers::sqrt2); }
namespace {
// exp(-x) * I0(x) for x >= 0 (Abramowitz and Stegun 9.8.1 and 9.8.2).
double scaled_bessel_i0(double x) {
    if (x < 3.75) {
        const double t = (x / 3.75) * (x / 3.75);
        const double i0 = 1 + t * (3.5156229 + t * (3.0899424 + t * (1.2067492 + t * (0.2659732 + t * (0.0360768 + t * 0.0045813)))));
        return i0 * std::exp(-x);
    }
    const double t = 3.75 / x;
    const double poly = 0.39894228 + t * (0.01328592 + t * (0.00225319 + t * (-0.00157565 + t * (0.00916281 + t * (-0.02057706 +
                        t * (0.02635537 + t * (-0.01647633 + t * 0.00392377)))))));
    return poly / std::sqrt(x);
}
// Marcum Q1(a, b) = integral from b to infinity of x exp(-(x^2 + a^2) / 2) I0(a x) dx, by Simpson's rule.
double marcum_q1(double a, double b) {
    const double upper = std::max(a, b) + 12;
    if (upper <= b) return 0;
    constexpr int steps = 4000; // Even.
    const double h = (upper - b) / steps;
    const auto f = [&](double x) { return x * std::exp(-0.5 * (x - a) * (x - a)) * scaled_bessel_i0(a * x); };
    double sum = f(b) + f(upper);
    for (int k = 1; k < steps; ++k) sum += f(b + k * h) * (k % 2 ? 4 : 2);
    return sum * h / 3;
}
double psk_ber(int bits, double gamma) { // Gray-coded M-PSK, M >= 4.
    const double m = std::pow(2.0, bits);
    return 2.0 / bits * q_function(std::sqrt(2.0 * bits * gamma) * std::sin(std::numbers::pi / m));
}
double square_qam_ber(int bits, double gamma) {
    const double m = std::pow(2.0, bits);
    return 4.0 / bits * (1 - 1 / std::sqrt(m)) * q_function(std::sqrt(3.0 * bits * gamma / (m - 1)));
}
}
std::optional<double> theoretical_ber(Modulation modulation, double eb_n0_db) {
    const double gamma = std::pow(10.0, eb_n0_db / 10);
    switch (modulation) {
    case Modulation::BPSK:
    case Modulation::QPSK:
    case Modulation::OQPSK: return q_function(std::sqrt(2 * gamma));
    case Modulation::PSK8: return psk_ber(3, gamma);
    case Modulation::QAM16: return square_qam_ber(4, gamma);
    case Modulation::QAM64: return square_qam_ber(6, gamma);
    case Modulation::QAM256: return square_qam_ber(8, gamma);
    case Modulation::PAM4: return 2.0 * (1 - 1.0 / 4) / 2 * q_function(std::sqrt(6.0 * 2 * gamma / (16 - 1)));
    case Modulation::OOK: return q_function(std::sqrt(gamma));
    case Modulation::ASK4: return 0.5 * 1.5 * q_function(std::sqrt(2 * gamma / 7)); // Unipolar levels 0..3, Gray labels.
    case Modulation::DBPSK: return 0.5 * std::exp(-gamma);
    case Modulation::DQPSK:
    case Modulation::PI4DQPSK: {
        // Differentially coherent Gray QPSK (Proakis, 8.6): Q1(a, b) - 1/2 I0(ab) exp(-(a^2 + b^2) / 2).
        const double root = std::sqrt(2 * gamma);
        const double a = root * std::sqrt(1 - 1 / std::numbers::sqrt2), b = root * std::sqrt(1 + 1 / std::numbers::sqrt2);
        return marcum_q1(a, b) - 0.5 * scaled_bessel_i0(a * b) * std::exp(-0.5 * (b - a) * (b - a));
    }
    default: return std::nullopt;
    }
}
}
