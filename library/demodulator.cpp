#include "demodulator.h"
#include "signal_analysis.h"
#include <bit>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <string>
#include <vector>
namespace iq {
namespace {
std::string label_bits(int label, int bps) {
    std::string bits(static_cast<std::size_t>(bps), '0');
    for (int k = 0; k < bps; ++k) bits[static_cast<std::size_t>(k)] = ((label >> (bps - 1 - k)) & 1) ? '1' : '0';
    return bits;
}
int label_of(const std::string& bits, std::size_t offset, int bps) {
    int label = 0;
    for (int k = 0; k < bps; ++k) label = (label << 1) | (bits[offset + static_cast<std::size_t>(k)] == '1' ? 1 : 0);
    return label;
}
}
BitErrors& BitErrors::operator+=(const BitErrors& other) {
    symbol_count += other.symbol_count;
    symbol_errors += other.symbol_errors;
    bit_count += other.bit_count;
    bit_errors += other.bit_errors;
    return *this;
}
bool is_differential(Modulation m) {
    return m == Modulation::DBPSK || m == Modulation::DQPSK || m == Modulation::PI4DQPSK || m == Modulation::DPSK8;
}
bool has_reference_demodulator(Modulation m) { return is_valid(m) && waveform_family(m) == Family::Linear; }
std::optional<BitErrors> bit_errors(const GeneratedSignal& signal, std::size_t first_symbol, std::size_t end_symbol) {
    const auto modulation = signal.config.modulation;
    if (signal.family != Family::Linear || !has_reference_demodulator(modulation) || signal.bits.empty()) return std::nullopt;
    const int bps = bits_per_symbol(modulation);
    const int labels = 1 << bps;
    const auto observations = matched_symbols(signal);
    if (signal.bits.size() != signal.symbols.size() * static_cast<std::size_t>(bps))
        throw std::invalid_argument("Signal bits do not match its symbols");

    // Ideal decision points (coherent) or ideal phase steps (differential), taken from the mapper itself.
    std::vector<std::complex<double>> reference(static_cast<std::size_t>(labels));
    const bool differential = is_differential(modulation);
    for (int label = 0; label < labels; ++label) {
        if (!differential) {
            reference[static_cast<std::size_t>(label)] =
                std::complex<double>(map_symbols(modulation, label_bits(label, bps))[0]) * signal.config.amplitude_gain;
        } else {
            // Symbol after label 0, then after label `label`: the ratio is the phase step of `label`.
            const auto pair = map_symbols(modulation, label_bits(0, bps) + label_bits(label, bps));
            reference[static_cast<std::size_t>(label)] = std::complex<double>(pair[1]) / std::complex<double>(pair[0]);
        }
    }

    BitErrors result;
    for (std::size_t i = 0; i < observations.values.size(); ++i) {
        const auto index = observations.symbol_indices[i];
        if (index < first_symbol || index >= end_symbol) continue;
        int decided = 0;
        double best = -1;
        if (!differential) {
            const std::complex<double> value(observations.values[i]);
            for (int label = 0; label < labels; ++label) {
                const double distance = std::norm(value - reference[static_cast<std::size_t>(label)]);
                if (best < 0 || distance < best) { best = distance; decided = label; }
            }
        } else {
            if (i == 0 || observations.symbol_indices[i - 1] + 1 != index || observations.symbol_indices[i - 1] < first_symbol) continue;
            const auto step = std::complex<double>(observations.values[i]) * std::conj(std::complex<double>(observations.values[i - 1]));
            double top = -2;
            for (int label = 0; label < labels; ++label) {
                const double match = std::real(step * std::conj(reference[static_cast<std::size_t>(label)]));
                if (match > top) { top = match; decided = label; }
            }
        }
        const int sent = label_of(signal.bits, index * static_cast<std::size_t>(bps), bps);
        ++result.symbol_count;
        result.bit_count += static_cast<std::size_t>(bps);
        if (decided != sent) {
            ++result.symbol_errors;
            result.bit_errors += static_cast<std::size_t>(std::popcount(static_cast<unsigned>(decided ^ sent)));
        }
    }
    if (result.symbol_count == 0) return std::nullopt;
    return result;
}
}
