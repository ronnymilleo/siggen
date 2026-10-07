#pragma once
#include "demodulator.h"
#include "generator.h"
#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
namespace iq {
struct BerSweepSettings {
    std::vector<double> eb_n0_db;          // Points to measure, in order.
    std::size_t min_errors = 100;          // Stop a point once this many bit errors are seen...
    std::size_t max_bits = 2'000'000;      // ...or this many bits have been compared.
    int block_symbols = 4096;              // Symbols per generated block (at most 65536).
};
struct BerPoint {
    double eb_n0_db = 0;
    double snr_db = 0;                     // Per-sample SNR actually applied.
    BitErrors errors;
    std::optional<double> theory;          // Textbook BER at this Eb/N0, where one exists.
    double ber() const { return errors.ber(); }
};
// Per-sample SNR that gives the requested Eb/N0 for this configuration.
double snr_for_eb_n0(const GenerationConfig& config, double eb_n0_db);
// Measures bit error rate with the reference demodulator across Eb/N0. The configuration supplies the
// waveform, pulse, rate, gain and impairments; data and noise seeds are derived per block, so a sweep
// is reproducible. Throws for waveforms without a reference demodulator. `cancel`, when set, aborts
// early and returns the points finished so far. `progress` receives the number of points done.
std::vector<BerPoint> ber_sweep(const GenerationConfig& config, const BerSweepSettings& settings,
                                const std::atomic<bool>* cancel = nullptr, std::atomic<int>* progress = nullptr);
// Table (text) and JSON renderings of a sweep, used by `siggen ber`.
std::string ber_text(const GenerationConfig& config, const std::vector<BerPoint>& points);
std::string ber_json(const GenerationConfig& config, const std::vector<BerPoint>& points);
}
