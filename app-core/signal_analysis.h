/**
 * @file    signal_analysis.h
 * @brief   Receiver-side analysis: matched-filter symbol observations, instantaneous frequency and Welch PSD.
 */

#ifndef SIGGEN_SIGNAL_ANALYSIS_H
#define SIGGEN_SIGNAL_ANALYSIS_H

#include "generator.h"
#include <complex>
#include <cstddef>
#include <vector>

namespace Core {

/**
 * @struct  SymbolObservations
 * @brief   Matched-filter outputs at the decision instants of the steady-state symbols.
 */
struct SymbolObservations {
    std::vector<std::complex<float>> Values;
    std::vector<std::size_t> SymbolIndices; // Index into GeneratedSignal::Symbols of each value
};

/**
 * @enum    Window
 * @brief   Periodic analysis windows for the Welch estimator.
 */
enum class Window {
    Hann,
    Hamming,
    Blackman,
    Rectangular
};

/**
 * @struct  Spectrum
 * @brief   Two-sided power spectral density estimate.
 */
struct Spectrum {
    std::vector<double> FrequencyHz;
    std::vector<double> PowerDensity; // Relative amplitude squared / Hz, two-sided
    std::size_t SegmentLength = 0;
    std::size_t SegmentCount = 0;
    Core::Window Window = Core::Window::Hann;
};

// Symbol observations and frequency estimate
SymbolObservations MatchedSymbols(const GeneratedSignal &signal);
std::vector<double> InstantaneousFrequency(const std::vector<std::complex<float>> &samples, double sample_rate_hz);

// Spectrum
const char *WindowName(Window window);
double WindowValue(Window window, std::size_t k, std::size_t length);
Spectrum WelchPsd(const std::vector<std::complex<float>> &samples, double sample_rate_hz,
                  std::size_t segment_length = 1024, Window window = Window::Hann);

} // namespace Core

#endif // SIGGEN_SIGNAL_ANALYSIS_H
