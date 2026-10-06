#pragma once
#include "generator.h"

#include <complex>
#include <cstddef>
#include <vector>
namespace iq {
struct SymbolObservations {
    std::vector<std::complex<float>> values;
    std::vector<std::size_t> symbol_indices;
};
SymbolObservations matched_symbols(const GeneratedSignal& signal);
// Frequency estimate between consecutive samples, arg(x[n+1] * conj(x[n])) * Fs / (2 pi),
// in Hz; one value fewer than the input. The principal phase difference is taken,
// so it is valid for |f| < Fs/2 and unaffected by phase wrapping.
std::vector<double> instantaneous_frequency(const std::vector<std::complex<float>>& samples, double sample_rate_hz);
}
namespace iq {
// Periodic analysis windows for the Welch estimator.
enum class Window { Hann, Hamming, Blackman, Rectangular };
const char* window_name(Window window);
// Window coefficient w[k] for k in [0, length); periodic (DFT-even) definition.
double window_value(Window window, std::size_t k, std::size_t length);
struct Spectrum {
    std::vector<double> frequency_hz;
    std::vector<double> power_density; // Relative amplitude squared / Hz, two-sided.
    std::size_t segment_length = 0;
    std::size_t segment_count = 0;
    Window window = Window::Hann;
};
Spectrum welch_psd(const std::vector<std::complex<float>>& samples, double sample_rate_hz, std::size_t segment_length = 1024,
                  Window window = Window::Hann);
}
