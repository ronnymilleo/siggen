/**
 * @file    measurements.h
 * @brief   Signal quality measurements: power statistics, symbol accuracy, eye diagram and energy ratios.
 */

#ifndef SIGGEN_MEASUREMENTS_H
#define SIGGEN_MEASUREMENTS_H

#include "generator.h"
#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

namespace Core {

/**
 * @struct  PowerStatistics
 * @brief   Amplitude statistics over the complete sample buffer (transients included).
 */
struct PowerStatistics {
    double MeanPower = 0; // Mean |x|^2 (relative amplitude squared)
    double PeakPower = 0; // Maximum |x|^2
    double PaprDb = 0;    // 10 log10(peak / mean); zero for an all-zero buffer
};

/**
 * @struct  SymbolAccuracy
 * @brief   Symbol-level accuracy of linear signals, from the matched-filter observations.
 * @details EVM is the RMS error vector magnitude normalized by the RMS ideal symbol magnitude (the mapped
 *          constellation scaled by the amplitude gain).
 */
struct SymbolAccuracy {
    std::size_t SymbolCount = 0;
    double EvmRms = 0;            // Fraction (0.05 = 5 %)
    double EvmDb = 0;             // 20 log10(evm_rms)
    double SnrAfterMatchedDb = 0; // -evm_db: SNR of the matched-filter observations
    double ExpectedOffsetDb = 0;  // 10 log10(SPS): sample SNR + offset = post-filter SNR
};

/**
 * @struct  EyeDiagram
 * @brief   Overlaid matched-filter traces for the eye diagram, each spanning two symbol periods centred on a
 *          symbol decision instant.
 */
struct EyeDiagram {
    std::vector<double> TimeSymbols;          // Offsets in symbol periods, -1 to +1 inclusive
    std::vector<std::vector<double>> InPhase; // One trace per steady-state symbol
    std::vector<std::vector<double>> Quadrature;
};

/**
 * @struct  EnergyRatios
 * @brief   Per-sample SNR re-expressed per symbol and per bit.
 * @details With complex noise power Pn over the sample rate Fs, N0 = Pn / Fs and Es = Ps / Rs, so
 *          Es/N0 = SNR * SPS and Eb/N0 = Es/N0 / bits per symbol.
 */
struct EnergyRatios {
    double EsN0Db = 0;
    double EbN0Db = 0;
};

PowerStatistics MeasurePowerStatistics(const std::vector<std::complex<float>> &samples);
std::optional<SymbolAccuracy> MeasureSymbolAccuracy(const GeneratedSignal &signal);
std::optional<SymbolAccuracy> MeasureSymbolAccuracy(const GeneratedSignal &signal, std::size_t first_symbol,
                                                    std::size_t end_symbol);
EyeDiagram BuildEyeDiagram(const GeneratedSignal &signal, std::size_t max_traces = 200);
EnergyRatios SnrToEnergyRatios(double snr_db, int samples_per_symbol, int bits_per_symbol);

} // namespace Core

#endif // SIGGEN_MEASUREMENTS_H
