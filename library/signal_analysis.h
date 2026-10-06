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
}
namespace iq {
struct Spectrum {
    std::vector<double> frequency_hz;
    std::vector<double> power_density; // Relative amplitude squared / Hz, two-sided.
    std::size_t segment_length = 0;
    std::size_t segment_count = 0;
};
Spectrum welch_psd(const std::vector<std::complex<float>>& samples, double sample_rate_hz, std::size_t segment_length = 1024);
}
