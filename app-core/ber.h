/**
 * @file    ber.h
 * @brief   Measures bit error rate curves with the reference demodulator and renders them for `siggen ber`.
 */

#ifndef SIGGEN_BER_H
#define SIGGEN_BER_H

#include "demodulator.h"
#include "generator.h"
#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  BerSweepSettings
 * @brief   Eb/N0 points of a BER sweep and when each point stops.
 */
struct BerSweepSettings {
    std::vector<double> EbN0Db;      // Points to measure, in order
    std::size_t MinErrors = 100;     // Stop a point once this many bit errors are seen...
    std::size_t MaxBits = 2'000'000; // ...or this many bits have been compared
    int BlockSymbols = 4096;         // Symbols per generated block (64 to 65536)
};

/**
 * @struct  BerPoint
 * @brief   One measured point of a BER sweep, with the textbook value next to it.
 */
struct BerPoint {
    double EbN0Db = 0;
    double SnrDb = 0; // Per-sample SNR actually applied
    BitErrors Errors;
    std::optional<double> Theory; // Textbook BER at this Eb/N0, where one exists

    double Ber() const;
};

double SnrForEbN0(const GenerationConfig &config, double eb_n0_db);
std::vector<BerPoint> BerSweep(const GenerationConfig &config, const BerSweepSettings &settings,
                               const std::atomic<bool> *cancel = nullptr, std::atomic<int> *progress = nullptr);

std::string BerText(const GenerationConfig &config, const std::vector<BerPoint> &points);
std::string BerJson(const GenerationConfig &config, const std::vector<BerPoint> &points);

} // namespace Core

#endif // SIGGEN_BER_H
