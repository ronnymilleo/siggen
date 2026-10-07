#include "analysis.h"
#include "iq_export.h"
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace iq {
AnalysisReport analyze_recording(const Recording& recording, Window window, std::size_t segment_length) {
    if (recording.samples.empty()) throw std::invalid_argument("Recording has no samples");
    AnalysisReport report;
    report.sample_count = recording.samples.size();
    report.sample_rate_hz = recording.sample_rate_hz;
    report.duration_s = static_cast<double>(report.sample_count) / recording.sample_rate_hz;
    report.power = power_statistics(recording.samples);
    const auto psd = welch_psd(recording.samples, recording.sample_rate_hz, segment_length, window);
    report.window = psd.window;
    report.segment_length = psd.segment_length;
    report.segment_count = psd.segment_count;
    double total = 0, peak = -1;
    std::size_t peak_bin = 0;
    for (std::size_t k = 0; k < psd.power_density.size(); ++k) {
        total += psd.power_density[k];
        if (psd.power_density[k] > peak) { peak = psd.power_density[k]; peak_bin = k; }
    }
    report.peak_frequency_hz = psd.frequency_hz[peak_bin];
    report.peak_density_db = 10 * std::log10(std::max(peak, 1e-20));
    if (total > 0) {
        // Edges where the cumulative power from either end reaches 0.5 %.
        double running = 0;
        std::size_t low = 0, high = psd.power_density.size() - 1;
        for (std::size_t k = 0; k < psd.power_density.size(); ++k) {
            running += psd.power_density[k];
            if (running >= 0.005 * total) { low = k; break; }
        }
        running = 0;
        for (std::size_t k = psd.power_density.size(); k-- > 0;) {
            running += psd.power_density[k];
            if (running >= 0.005 * total) { high = k; break; }
        }
        const double bin = psd.frequency_hz.size() > 1 ? psd.frequency_hz[1] - psd.frequency_hz[0] : 0;
        report.occupied_bandwidth_hz = high >= low ? psd.frequency_hz[high] - psd.frequency_hz[low] + bin : 0;
    }
    if (!recording.config) {
        report.note = "No siggen configuration in the file: EVM needs a recording produced by siggen (SigMF).";
        return report;
    }
    report.waveform = modulation_name(recording.config->modulation);
    const auto signal = signal_from_recording(recording);
    if (!signal) {
        report.note = "The sample count does not match the embedded configuration: EVM skipped.";
        return report;
    }
    report.accuracy = symbol_accuracy(*signal);
    if (!report.accuracy) report.note = "This waveform has no symbol constellation: EVM does not apply.";
    else report.sample_snr_db = report.accuracy->snr_after_matched_db - report.accuracy->expected_offset_db;
    return report;
}
std::string report_text(const AnalysisReport& r) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(6);
    out << "samples:            " << r.sample_count << "\nsample rate:        " << r.sample_rate_hz << " Hz\nduration:           " << r.duration_s << " s\n"
        << "mean power:         " << r.power.mean_power << "\npeak power:         " << r.power.peak_power
        << "\nPAPR:               " << r.power.papr_db << " dB\n"
        << "spectrum peak:      " << r.peak_frequency_hz << " Hz (" << r.peak_density_db << " dB re 1/Hz)\n"
        << "99% bandwidth:      " << r.occupied_bandwidth_hz << " Hz\n"
        << "welch:              " << window_name(r.window) << " window, " << r.segment_count << " x " << r.segment_length << " samples\n";
    if (r.waveform) out << "waveform:           " << *r.waveform << "\n";
    if (r.accuracy)
        out << "symbols:            " << r.accuracy->symbol_count << "\nEVM:                " << r.accuracy->evm_rms * 100 << " % (" << r.accuracy->evm_db << " dB)\n"
            << "SNR after matched:  " << r.accuracy->snr_after_matched_db << " dB\nSNR per sample:     " << r.sample_snr_db << " dB\n";
    if (!r.note.empty()) out << "note:               " << r.note << "\n";
    return out.str();
}
std::string report_json(const AnalysisReport& r) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "{\n  \"sample_count\": " << r.sample_count << ",\n  \"sample_rate_hz\": " << r.sample_rate_hz
        << ",\n  \"duration_s\": " << r.duration_s << ",\n  \"mean_power\": " << r.power.mean_power
        << ",\n  \"peak_power\": " << r.power.peak_power << ",\n  \"papr_db\": " << r.power.papr_db
        << ",\n  \"peak_frequency_hz\": " << r.peak_frequency_hz << ",\n  \"peak_density_db\": " << r.peak_density_db
        << ",\n  \"occupied_bandwidth_hz\": " << r.occupied_bandwidth_hz
        << ",\n  \"welch\": {\"window\": " << json_quote(window_name(r.window)) << ", \"segment_length\": " << r.segment_length
        << ", \"segment_count\": " << r.segment_count << "}";
    if (r.waveform) out << ",\n  \"waveform\": " << json_quote(*r.waveform);
    if (r.accuracy)
        out << ",\n  \"symbol_count\": " << r.accuracy->symbol_count << ",\n  \"evm_rms\": " << r.accuracy->evm_rms
            << ",\n  \"evm_db\": " << r.accuracy->evm_db << ",\n  \"snr_after_matched_db\": " << r.accuracy->snr_after_matched_db
            << ",\n  \"sample_snr_db\": " << r.sample_snr_db;
    if (!r.note.empty()) out << ",\n  \"note\": " << json_quote(r.note);
    out << "\n}\n";
    return out.str();
}
}
