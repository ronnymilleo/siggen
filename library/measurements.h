#pragma once
#include "generator.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace iq {
// Amplitude statistics over the complete sample buffer (transients included).
struct PowerStatistics {
    double mean_power = 0;  // Mean |x|^2 (relative amplitude squared).
    double peak_power = 0;  // Maximum |x|^2.
    double papr_db = 0;     // 10 log10(peak / mean); zero for an all-zero buffer.
};
PowerStatistics power_statistics(const std::vector<std::complex<float>>& samples);

// Symbol-level accuracy of linear signals, from the matched-filter observations.
// EVM is the RMS error vector magnitude normalized by the RMS ideal symbol
// magnitude (the mapped constellation scaled by the amplitude gain).
struct SymbolAccuracy {
    std::size_t symbol_count = 0;
    double evm_rms = 0;                 // Fraction (0.05 = 5 %).
    double evm_db = 0;                  // 20 log10(evm_rms).
    double snr_after_matched_db = 0;    // -evm_db: SNR of the matched-filter observations.
    double expected_offset_db = 0;      // 10 log10(SPS): sample SNR + offset = post-filter SNR.
};
// Empty for noise sources and for signals without steady-state symbols.
std::optional<SymbolAccuracy> symbol_accuracy(const GeneratedSignal& signal);
// Same, restricted to symbols with index in [first_symbol, end_symbol); used to score the part of a
// buffer that carries real data (for example the interior of a batch frame).
std::optional<SymbolAccuracy> symbol_accuracy(const GeneratedSignal& signal, std::size_t first_symbol, std::size_t end_symbol);

// Overlaid matched-filter traces, each spanning two symbol periods centred on
// a symbol decision instant, for the eye diagram.
struct EyeDiagram {
    std::vector<double> time_symbols;          // Offsets in symbol periods, -1 to +1 inclusive.
    std::vector<std::vector<double>> in_phase; // One trace per steady-state symbol.
    std::vector<std::vector<double>> quadrature;
};
// Throws for noise sources. `max_traces` bounds the cost; traces are taken
// uniformly across the steady-state symbols. Returns empty traces if there are none.
EyeDiagram eye_diagram(const GeneratedSignal& signal, std::size_t max_traces = 200);

// Per-sample SNR re-expressed per symbol and per bit. With complex noise power
// Pn over the sample rate Fs, N0 = Pn / Fs and Es = Ps / Rs, so
// Es/N0 = SNR * SPS and Eb/N0 = Es/N0 / bits per symbol.
struct EnergyRatios {
    double es_n0_db = 0;
    double eb_n0_db = 0;
};
EnergyRatios snr_to_energy_ratios(double snr_db, int samples_per_symbol, int bits_per_symbol);
}
