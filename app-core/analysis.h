/**
 * @file    analysis.h
 * @brief   Analyzes a recording for `siggen analyze`: power, spectrum, EVM and bit errors, as text or JSON.
 */

#ifndef SIGGEN_ANALYSIS_H
#define SIGGEN_ANALYSIS_H

#include "demodulator.h"
#include "measurements.h"
#include "recording.h"
#include "signal_analysis.h"
#include <cstddef>
#include <optional>
#include <string>

namespace Core {

/**
 * @struct  AnalysisReport
 * @brief   What `siggen analyze` reports about a recording, using the same estimators as the GUI.
 * @details The symbol accuracy fields need siggen's configuration in the file with a matching length; the bit
 *          error fields also need a linear waveform.
 */
struct AnalysisReport {
    std::size_t SampleCount = 0;
    double SampleRateHz = 0;
    double DurationS = 0;
    PowerStatistics Power;
    double PeakFrequencyHz = 0;     // Frequency of the strongest Welch bin
    double PeakDensityDb = 0;       // That bin, 10 log10 of relative amplitude^2 / Hz
    double OccupiedBandwidthHz = 0; // Span containing the central 99 % of the power
    Core::Window Window = Core::Window::Hann;
    std::size_t SegmentLength = 0;
    std::size_t SegmentCount = 0;
    std::optional<std::string> Waveform; // Only when the file carries siggen's configuration
    std::optional<SymbolAccuracy> Accuracy;
    double SampleSnrDb = 0;          // Per-sample SNR implied by the post-filter SNR (valid with `Accuracy`)
    std::optional<BitErrors> Errors; // Reference demodulator against the transmitted bits (linear, with `Accuracy`)
    double MeasuredEbN0Db = 0;       // Eb/N0 implied by the measured SNR (valid with `Errors`)
    std::optional<double> TheoreticalBer; // Textbook BER at that Eb/N0, where a closed form exists
    std::string Note;                     // Why EVM is absent, when it is
};

AnalysisReport AnalyzeRecording(const Recording &recording, Window window = Window::Hann,
                                std::size_t segment_length = 1024);

std::string ReportText(const AnalysisReport &report);
std::string ReportJson(const AnalysisReport &report);

} // namespace Core

#endif // SIGGEN_ANALYSIS_H
