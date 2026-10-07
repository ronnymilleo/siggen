#pragma once
#include "measurements.h"
#include "recording.h"
#include "signal_analysis.h"
#include <cstddef>
#include <optional>
#include <string>
namespace iq {
// What `siggen analyze` reports about a recording, using the same estimators as the GUI.
struct AnalysisReport {
    std::size_t sample_count = 0;
    double sample_rate_hz = 0;
    double duration_s = 0;
    PowerStatistics power;
    double peak_frequency_hz = 0;       // Frequency of the strongest Welch bin.
    double peak_density_db = 0;         // That bin, 10 log10 of relative amplitude^2 / Hz.
    double occupied_bandwidth_hz = 0;   // Span containing the central 99 % of the power.
    Window window = Window::Hann;
    std::size_t segment_length = 0;
    std::size_t segment_count = 0;
    // Only when the file carries siggen's configuration and its length matches it.
    std::optional<std::string> waveform;
    std::optional<SymbolAccuracy> accuracy;
    double sample_snr_db = 0;           // Per-sample SNR implied by the post-filter SNR (valid with `accuracy`).
    std::string note;                   // Why EVM is absent, when it is.
};
AnalysisReport analyze_recording(const Recording& recording, Window window = Window::Hann, std::size_t segment_length = 1024);
std::string report_text(const AnalysisReport& report);
std::string report_json(const AnalysisReport& report);
}
