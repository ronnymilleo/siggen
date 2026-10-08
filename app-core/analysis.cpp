/**
 * @file    analysis.cpp
 * @brief   Analyzes a recording for `siggen analyze`: power, spectrum, EVM and bit errors, as text or JSON.
 */

#include "analysis.h"

#include "iq_export.h"
#include "theory.h"
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace Core {

namespace {

// Width between the edges where the cumulative power from either end reaches 0.5 %
double OccupiedBandwidth(const Spectrum &psd, double total_power) {
    const auto bin_count = psd.PowerDensity.size();
    double running = 0;
    std::size_t low = 0;
    std::size_t high = bin_count - 1;
    for (std::size_t k = 0; k < bin_count; ++k) {
        running += psd.PowerDensity[k];
        if (running >= 0.005 * total_power) {
            low = k;
            break;
        }
    }
    running = 0;
    for (std::size_t k = bin_count; k-- > 0;) {
        running += psd.PowerDensity[k];
        if (running >= 0.005 * total_power) {
            high = k;
            break;
        }
    }
    const double bin_width = psd.FrequencyHz.size() > 1 ? psd.FrequencyHz[1] - psd.FrequencyHz[0] : 0;
    return high >= low ? psd.FrequencyHz[high] - psd.FrequencyHz[low] + bin_width : 0;
}

void MeasureSpectrum(const Recording &recording, Window window, std::size_t segment_length, AnalysisReport &report) {
    const auto psd = WelchPsd(recording.Samples, recording.SampleRateHz, segment_length, window);
    report.Window = psd.Window;
    report.SegmentLength = psd.SegmentLength;
    report.SegmentCount = psd.SegmentCount;
    double total_power = 0;
    double peak_density = -1;
    std::size_t peak_bin = 0;
    for (std::size_t k = 0; k < psd.PowerDensity.size(); ++k) {
        total_power += psd.PowerDensity[k];
        if (psd.PowerDensity[k] > peak_density) {
            peak_density = psd.PowerDensity[k];
            peak_bin = k;
        }
    }
    report.PeakFrequencyHz = psd.FrequencyHz[peak_bin];
    report.PeakDensityDb = 10 * std::log10(std::max(peak_density, 1e-20));
    if (total_power > 0) {
        report.OccupiedBandwidthHz = OccupiedBandwidth(psd, total_power);
    }
}

// Derives the per-sample SNR, the measured Eb/N0 and the textbook BER from a measured symbol accuracy
void FinishAccuracy(AnalysisReport &report, int samples_per_symbol, Modulation modulation) {
    report.SampleSnrDb = report.Accuracy->SnrAfterMatchedDb - report.Accuracy->ExpectedOffsetDb;
    report.MeasuredEbN0Db = SnrToEnergyRatios(report.SampleSnrDb, samples_per_symbol, BitsPerSymbol(modulation)).EbN0Db;
    report.TheoreticalBer = TheoreticalBer(modulation, report.MeasuredEbN0Db);
}

// Scores only the symbols of a batch frame whose matched-filter windows lie inside the frame
void MeasureFrameAccuracy(const Recording &recording, AnalysisReport &report) {
    if (const auto scoring = ScoreFrame(recording)) {
        report.Accuracy = MeasureSymbolAccuracy(scoring->Signal, scoring->FirstSymbol, scoring->EndSymbol);
        if (report.Accuracy) {
            report.Errors = CountBitErrors(scoring->Signal, scoring->FirstSymbol, scoring->EndSymbol);
        }
    }
    if (!report.Accuracy) {
        report.Note =
            "No symbol of this batch frame has its matched-filter window fully inside the frame: EVM skipped.";
    } else {
        FinishAccuracy(report, recording.Config->SamplesPerSymbol, recording.Config->Modulation);
    }
}

void MeasureSignalAccuracy(const Recording &recording, AnalysisReport &report) {
    const auto signal = SignalFromRecording(recording);
    if (!signal) {
        report.Note = "The sample count does not match the embedded configuration: EVM skipped.";
        return;
    }
    report.Accuracy = MeasureSymbolAccuracy(*signal);
    if (report.Accuracy) {
        report.Errors = CountBitErrors(*signal);
    }
    if (!report.Accuracy) {
        report.Note = "This waveform has no symbol constellation: EVM does not apply.";
    } else {
        FinishAccuracy(report, recording.Config->SamplesPerSymbol, recording.Config->Modulation);
    }
}

} // namespace

/**
 * @brief   Analyzes a recording with the same estimators as the GUI.
 * @param[in] recording       Recording read by ReadRecording().
 * @param[in] window          Window of the Welch spectrum.
 * @param[in] segment_length  Welch segment length, in samples.
 * @return  Power and spectrum figures for any recording; symbol accuracy and bit errors when the file carries
 *          siggen's configuration, otherwise a note saying why they are absent.
 * @note    Throws std::invalid_argument for a recording without samples.
 */
AnalysisReport AnalyzeRecording(const Recording &recording, Window window, std::size_t segment_length) {
    if (recording.Samples.empty()) {
        throw std::invalid_argument("Recording has no samples");
    }
    AnalysisReport report;
    report.SampleCount = recording.Samples.size();
    report.SampleRateHz = recording.SampleRateHz;
    report.DurationS = static_cast<double>(report.SampleCount) / recording.SampleRateHz;
    report.Power = MeasurePowerStatistics(recording.Samples);
    MeasureSpectrum(recording, window, segment_length, report);
    if (!recording.Config) {
        report.Note = "No siggen configuration in the file: EVM needs a recording produced by siggen (SigMF).";
        return report;
    }
    report.Waveform = ModulationName(recording.Config->Modulation);
    if (recording.Frame) {
        MeasureFrameAccuracy(recording, report);
    } else {
        MeasureSignalAccuracy(recording, report);
    }
    return report;
}

/**
 * @brief   Renders an analysis report as the aligned text printed by `siggen analyze`.
 * @param[in] report  Report from AnalyzeRecording().
 * @return  One "label: value" line per figure, with 6 significant digits.
 */
std::string ReportText(const AnalysisReport &report) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(6);
    out << "samples:            " << report.SampleCount << "\nsample rate:        " << report.SampleRateHz
        << " Hz\nduration:           " << report.DurationS << " s\n"
        << "mean power:         " << report.Power.MeanPower << "\npeak power:         " << report.Power.PeakPower
        << "\nPAPR:               " << report.Power.PaprDb << " dB\n"
        << "spectrum peak:      " << report.PeakFrequencyHz << " Hz (" << report.PeakDensityDb << " dB re 1/Hz)\n"
        << "99% bandwidth:      " << report.OccupiedBandwidthHz << " Hz\n"
        << "welch:              " << WindowName(report.Window) << " window, " << report.SegmentCount << " x "
        << report.SegmentLength << " samples\n";
    if (report.Waveform) {
        out << "waveform:           " << *report.Waveform << "\n";
    }
    if (report.Accuracy) {
        out << "symbols:            " << report.Accuracy->SymbolCount
            << "\nEVM:                " << report.Accuracy->EvmRms * 100 << " % (" << report.Accuracy->EvmDb << " dB)\n"
            << "SNR after matched:  " << report.Accuracy->SnrAfterMatchedDb
            << " dB\nSNR per sample:     " << report.SampleSnrDb << " dB\n"
            << "Eb/N0 (measured):   " << report.MeasuredEbN0Db << " dB\n";
    }
    if (report.Errors) {
        out << "bits compared:      " << report.Errors->BitCount
            << "\nbit errors:         " << report.Errors->BitErrorCount
            << "\nBER:                " << report.Errors->Ber() << "\nSER:                " << report.Errors->Ser()
            << "\n";
    }
    if (report.Errors && report.TheoreticalBer) {
        out << "theoretical BER:    " << *report.TheoreticalBer << " (ideal receiver at the measured Eb/N0)\n";
    }
    if (!report.Note.empty()) {
        out << "note:               " << report.Note << "\n";
    }
    return out.str();
}

/**
 * @brief   Renders an analysis report as the JSON object printed by `siggen analyze --json`.
 * @param[in] report  Report from AnalyzeRecording().
 * @return  A JSON object with full double precision; optional members appear only when measured.
 */
std::string ReportJson(const AnalysisReport &report) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "{\n  \"sample_count\": " << report.SampleCount << ",\n  \"sample_rate_hz\": " << report.SampleRateHz
        << ",\n  \"duration_s\": " << report.DurationS << ",\n  \"mean_power\": " << report.Power.MeanPower
        << ",\n  \"peak_power\": " << report.Power.PeakPower << ",\n  \"papr_db\": " << report.Power.PaprDb
        << ",\n  \"peak_frequency_hz\": " << report.PeakFrequencyHz
        << ",\n  \"peak_density_db\": " << report.PeakDensityDb
        << ",\n  \"occupied_bandwidth_hz\": " << report.OccupiedBandwidthHz
        << ",\n  \"welch\": {\"window\": " << JsonQuote(WindowName(report.Window))
        << ", \"segment_length\": " << report.SegmentLength << ", \"segment_count\": " << report.SegmentCount << "}";
    if (report.Waveform) {
        out << ",\n  \"waveform\": " << JsonQuote(*report.Waveform);
    }
    if (report.Accuracy) {
        out << ",\n  \"symbol_count\": " << report.Accuracy->SymbolCount
            << ",\n  \"evm_rms\": " << report.Accuracy->EvmRms << ",\n  \"evm_db\": " << report.Accuracy->EvmDb
            << ",\n  \"snr_after_matched_db\": " << report.Accuracy->SnrAfterMatchedDb
            << ",\n  \"sample_snr_db\": " << report.SampleSnrDb << ",\n  \"eb_n0_db\": " << report.MeasuredEbN0Db;
    }
    if (report.Errors) {
        out << ",\n  \"bit_count\": " << report.Errors->BitCount
            << ",\n  \"bit_errors\": " << report.Errors->BitErrorCount << ",\n  \"ber\": " << report.Errors->Ber()
            << ",\n  \"symbol_errors\": " << report.Errors->SymbolErrorCount
            << ",\n  \"ser\": " << report.Errors->Ser();
    }
    if (report.Errors && report.TheoreticalBer) {
        out << ",\n  \"theoretical_ber\": " << *report.TheoreticalBer;
    }
    if (!report.Note.empty()) {
        out << ",\n  \"note\": " << JsonQuote(report.Note);
    }
    out << "\n}\n";
    return out.str();
}

} // namespace Core
