#include "ber.h"
#include "measurements.h"
#include "iq_export.h"
#include "theory.h"
#include <iomanip>
#include <limits>
#include <sstream>
#include <cmath>
#include <random>
#include <stdexcept>
namespace iq {
double snr_for_eb_n0(const GenerationConfig& config, double eb_n0_db) {
    const int bps = bits_per_symbol(config.modulation);
    return eb_n0_db + 10.0 * std::log10(static_cast<double>(bps) / static_cast<double>(config.samples_per_symbol));
}
namespace {
std::uint32_t block_seed(std::uint32_t base, std::uint32_t tag, std::size_t point, std::size_t block) {
    std::seed_seq seq{base, tag, static_cast<std::uint32_t>(point), static_cast<std::uint32_t>(block)};
    std::uint32_t out = 0;
    seq.generate(&out, &out + 1);
    return out;
}
}
std::vector<BerPoint> ber_sweep(const GenerationConfig& config, const BerSweepSettings& settings,
                                const std::atomic<bool>* cancel, std::atomic<int>* progress) {
    if (!has_reference_demodulator(config.modulation)) throw std::invalid_argument("This waveform has no reference demodulator");
    if (settings.block_symbols < 64 || settings.block_symbols > 65536) throw std::invalid_argument("Block size must be 64 to 65536 symbols");
    if (settings.max_bits == 0) throw std::invalid_argument("Maximum bits must be positive");
    std::vector<BerPoint> points;
    for (std::size_t p = 0; p < settings.eb_n0_db.size(); ++p) {
        if (cancel && cancel->load()) break;
        BerPoint point;
        point.eb_n0_db = settings.eb_n0_db[p];
        point.snr_db = snr_for_eb_n0(config, point.eb_n0_db);
        point.theory = theoretical_ber(config.modulation, point.eb_n0_db);
        GenerationConfig block = config;
        block.symbol_count = settings.block_symbols;
        block.data_source = DataSource::Random;
        block.bits.clear();
        block.awgn.enabled = true;
        block.awgn.snr_db = point.snr_db;
        bool aborted = false;
        for (std::size_t b = 0; point.errors.bit_count < settings.max_bits && point.errors.bit_errors < settings.min_errors; ++b) {
            if (cancel && cancel->load()) { aborted = true; break; }
            block.seed = block_seed(config.seed, 1, p, b);
            block.noise_seed = block_seed(config.noise_seed, 2, p, b);
            block.impairment_seed = block_seed(config.impairment_seed, 3, p, b);
            if (const auto errors = bit_errors(generate(block))) point.errors += *errors;
            else break;
        }
        if (aborted) break;
        points.push_back(point);
        if (progress) progress->store(static_cast<int>(points.size()));
    }
    return points;
}
std::string ber_text(const GenerationConfig& config, const std::vector<BerPoint>& points) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "waveform: " << modulation_name(config.modulation) << ", " << config.samples_per_symbol << " samples/symbol, "
        << (config.pulse == Pulse::RRC ? "RRC" : "rectangular") << " pulse, ideal reference receiver\n";
    out << " Eb/N0 dB   SNR dB       bits     errors          BER       theory\n";
    for (const auto& p : points) {
        out << std::fixed << std::setw(9) << std::setprecision(2) << p.eb_n0_db << std::setw(10) << p.snr_db << std::defaultfloat
            << std::setw(11) << p.errors.bit_count << std::setw(11) << p.errors.bit_errors << std::setw(13) << std::setprecision(3)
            << std::scientific << p.ber();
        if (p.theory) out << std::setw(13) << *p.theory;
        else out << std::setw(13) << "-";
        out << std::defaultfloat << "\n";
    }
    return out.str();
}
std::string ber_json(const GenerationConfig& config, const std::vector<BerPoint>& points) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "{\n  \"waveform\": " << json_quote(modulation_name(config.modulation)) << ",\n  \"samples_per_symbol\": "
        << config.samples_per_symbol << ",\n  \"points\": [";
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& p = points[i];
        out << (i ? ",\n" : "\n") << "    {\"eb_n0_db\": " << p.eb_n0_db << ", \"snr_db\": " << p.snr_db << ", \"bits\": " << p.errors.bit_count
            << ", \"bit_errors\": " << p.errors.bit_errors << ", \"ber\": " << p.ber() << ", \"symbol_errors\": " << p.errors.symbol_errors
            << ", \"ser\": " << p.errors.ser() << ", \"theoretical_ber\": ";
        if (p.theory) out << *p.theory;
        else out << "null";
        out << "}";
    }
    out << "\n  ]\n}\n";
    return out.str();
}
}
