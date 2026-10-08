/**
 * @file    ber.cpp
 * @brief   Measures bit error rate curves with the reference demodulator and renders them for `siggen ber`.
 */

#include "ber.h"

#include "iq_export.h"
#include "measurements.h"
#include "theory.h"
#include <cmath>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>

namespace Core {

namespace {

bool Cancelled(const std::atomic<bool> *cancel) {
    return cancel && cancel->load();
}

// Seed of one block of one point; `tag` keeps the data, noise and impairment streams apart
std::uint32_t BlockSeed(std::uint32_t base_seed, std::uint32_t tag, std::size_t point_index, std::size_t block_index) {
    std::seed_seq sequence{base_seed, tag, static_cast<std::uint32_t>(point_index),
                           static_cast<std::uint32_t>(block_index)};
    std::uint32_t seed = 0;
    sequence.generate(&seed, &seed + 1);
    return seed;
}

// Random-data blocks of the sweep configuration with AWGN at the point's SNR
GenerationConfig BlockConfig(const GenerationConfig &config, const BerSweepSettings &settings, double snr_db) {
    GenerationConfig block = config;
    block.SymbolCount = settings.BlockSymbols;
    block.DataSource = DataSource::Random;
    block.Bits.clear();
    block.Awgn.Enabled = true;
    block.Awgn.SnrDb = snr_db;
    return block;
}

// Accumulates blocks until the point has enough errors or bits; empty when cancelled midway
std::optional<BerPoint> MeasurePoint(const GenerationConfig &config, const BerSweepSettings &settings,
                                     std::size_t point_index, const std::atomic<bool> *cancel) {
    BerPoint point;
    point.EbN0Db = settings.EbN0Db[point_index];
    point.SnrDb = SnrForEbN0(config, point.EbN0Db);
    point.Theory = TheoreticalBer(config.Modulation, point.EbN0Db);
    auto block = BlockConfig(config, settings, point.SnrDb);
    for (std::size_t block_index = 0;
         point.Errors.BitCount < settings.MaxBits && point.Errors.BitErrorCount < settings.MinErrors; ++block_index) {
        if (Cancelled(cancel)) {
            return std::nullopt;
        }
        block.Seed = BlockSeed(config.Seed, 1, point_index, block_index);
        block.NoiseSeed = BlockSeed(config.NoiseSeed, 2, point_index, block_index);
        block.ImpairmentSeed = BlockSeed(config.ImpairmentSeed, 3, point_index, block_index);
        if (const auto errors = CountBitErrors(Generate(block))) {
            point.Errors += *errors;
        } else {
            break;
        }
    }
    return point;
}

} // namespace

/**
 * @brief   Bit error rate of the point.
 * @return  Bit errors over bits compared; zero when no bit was compared.
 */
double BerPoint::Ber() const {
    return Errors.Ber();
}

/**
 * @brief   Per-sample SNR that gives the requested Eb/N0 for a configuration.
 * @param[in] config    Configuration supplying the waveform and the samples per symbol.
 * @param[in] eb_n0_db  Requested Eb/N0, in dB.
 * @return  The per-sample SNR, in dB.
 */
double SnrForEbN0(const GenerationConfig &config, double eb_n0_db) {
    const int bits_per_symbol = BitsPerSymbol(config.Modulation);
    return eb_n0_db +
           10.0 * std::log10(static_cast<double>(bits_per_symbol) / static_cast<double>(config.SamplesPerSymbol));
}

/**
 * @brief   Measures bit error rate with the reference demodulator across Eb/N0.
 * @param[in]  config    Supplies the waveform, pulse, rate, gain and impairments.
 * @param[in]  settings  Eb/N0 points and stopping rules.
 * @param[in]  cancel    When set, aborts early; the points finished so far are returned.
 * @param[out] progress  When given, receives the number of points done.
 * @return  One point per Eb/N0 value, in order.
 * @note    Data and noise seeds are derived per block, so a sweep is reproducible. Throws std::invalid_argument for
 *          waveforms without a reference demodulator, a block size outside 64 to 65536 symbols, or zero maximum
 *          bits.
 */
std::vector<BerPoint> BerSweep(const GenerationConfig &config, const BerSweepSettings &settings,
                               const std::atomic<bool> *cancel, std::atomic<int> *progress) {
    if (!HasReferenceDemodulator(config.Modulation)) {
        throw std::invalid_argument("This waveform has no reference demodulator");
    }
    if (settings.BlockSymbols < 64 || settings.BlockSymbols > 65536) {
        throw std::invalid_argument("Block size must be 64 to 65536 symbols");
    }
    if (settings.MaxBits == 0) {
        throw std::invalid_argument("Maximum bits must be positive");
    }
    std::vector<BerPoint> points;
    for (std::size_t point_index = 0; point_index < settings.EbN0Db.size(); ++point_index) {
        if (Cancelled(cancel)) {
            break;
        }
        const auto point = MeasurePoint(config, settings, point_index, cancel);
        if (!point) {
            break;
        }
        points.push_back(*point);
        if (progress) {
            progress->store(static_cast<int>(points.size()));
        }
    }
    return points;
}

/**
 * @brief   Renders a sweep as the table printed by `siggen ber`.
 * @param[in] config  Configuration of the sweep, described in the heading line.
 * @param[in] points  Measured points.
 * @return  A heading line, a column header and one row per point; "-" where no theory exists.
 */
std::string BerText(const GenerationConfig &config, const std::vector<BerPoint> &points) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "waveform: " << ModulationName(config.Modulation) << ", " << config.SamplesPerSymbol << " samples/symbol, "
        << (config.Pulse == Pulse::RRC ? "RRC" : "rectangular") << " pulse, ideal reference receiver\n";
    out << " Eb/N0 dB   SNR dB       bits     errors          BER       theory\n";
    for (const auto &point : points) {
        out << std::fixed << std::setw(9) << std::setprecision(2) << point.EbN0Db << std::setw(10) << point.SnrDb
            << std::defaultfloat << std::setw(11) << point.Errors.BitCount << std::setw(11)
            << point.Errors.BitErrorCount << std::setw(13) << std::setprecision(3) << std::scientific << point.Ber();
        if (point.Theory) {
            out << std::setw(13) << *point.Theory;
        } else {
            out << std::setw(13) << "-";
        }
        out << std::defaultfloat << "\n";
    }
    return out.str();
}

/**
 * @brief   Renders a sweep as the JSON object printed by `siggen ber --json`.
 * @param[in] config  Configuration of the sweep.
 * @param[in] points  Measured points.
 * @return  A JSON object with full double precision; `theoretical_ber` is null where no theory exists.
 */
std::string BerJson(const GenerationConfig &config, const std::vector<BerPoint> &points) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "{\n  \"waveform\": " << JsonQuote(ModulationName(config.Modulation))
        << ",\n  \"samples_per_symbol\": " << config.SamplesPerSymbol << ",\n  \"points\": [";
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto &point = points[i];
        out << (i ? ",\n" : "\n") << "    {\"eb_n0_db\": " << point.EbN0Db << ", \"snr_db\": " << point.SnrDb
            << ", \"bits\": " << point.Errors.BitCount << ", \"bit_errors\": " << point.Errors.BitErrorCount
            << ", \"ber\": " << point.Ber() << ", \"symbol_errors\": " << point.Errors.SymbolErrorCount
            << ", \"ser\": " << point.Errors.Ser() << ", \"theoretical_ber\": ";
        if (point.Theory) {
            out << *point.Theory;
        } else {
            out << "null";
        }
        out << "}";
    }
    out << "\n  ]\n}\n";
    return out.str();
}

} // namespace Core
