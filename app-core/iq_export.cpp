/**
 * @file    iq_export.cpp
 * @brief   Writes generated I/Q samples as CSV with a JSON sidecar, or as a SigMF recording.
 */

#include "iq_export.h"

#include "impairments.h"
#include "preset.h"
#include "signal_processing.h"
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>

namespace Core {

namespace {

bool IsSupported(const ExportFormat format) {
    return format == ExportFormat::CSV || format == ExportFormat::SigMF;
}

std::expected<void, std::string> ValidateLinear(const GeneratedSignal &signal) {
    const auto &config = signal.Config;
    const auto samples_per_symbol = static_cast<std::size_t>(config.SamplesPerSymbol);
    const auto taps =
        UsesRrc(config) ? static_cast<std::size_t>(config.SpanSymbols) * samples_per_symbol + 1 : samples_per_symbol;
    if (signal.Samples.size() != static_cast<std::size_t>(config.SymbolCount - 1) * samples_per_symbol + taps +
                                     QuadratureDelaySamples(config) ||
        signal.Symbols.size() != static_cast<std::size_t>(config.SymbolCount) ||
        signal.SampleRateHz != config.SymbolRateBaud * samples_per_symbol ||
        signal.FilterDelaySamples != (UsesRrc(config) ? (taps - 1) / 2 : 0)) {
        return std::unexpected(std::string("Result metadata does not match configuration"));
    }
    return {};
}

std::expected<void, std::string> ValidateFsk(const GeneratedSignal &signal) {
    const auto &config = signal.Config;
    const auto samples_per_symbol = static_cast<std::size_t>(config.SamplesPerSymbol);
    if (signal.Samples.size() != static_cast<std::size_t>(config.SymbolCount) * samples_per_symbol ||
        !signal.Symbols.empty() || signal.SymbolFrequenciesHz.size() != static_cast<std::size_t>(config.SymbolCount) ||
        signal.SampleRateHz != config.SymbolRateBaud * samples_per_symbol || signal.FilterDelaySamples != 0) {
        return std::unexpected(std::string("FSK result metadata does not match configuration"));
    }
    return {};
}

std::expected<void, std::string> ValidateNoise(const GeneratedSignal &signal) {
    const auto &source = signal.Config.NoiseSource;
    if (signal.Samples.size() != static_cast<std::size_t>(source.SampleCount) || !signal.Symbols.empty() ||
        signal.SampleRateHz != source.SampleRateHz || signal.FilterDelaySamples != 0) {
        return std::unexpected(std::string("Noise result metadata does not match configuration"));
    }
    return {};
}

/**
 * @brief   Checks that a signal is consistent with its own configuration and can be exported.
 * @return  Nothing on success; the configuration is validated first, then the format, the family, the
 *          family-specific sizes and finally the sample values.
 */
std::expected<void, std::string> ValidateResult(const GeneratedSignal &signal, const ExportFormat format) {
    if (const auto validated = Validate(signal.Config); !validated.has_value()) {
        return validated;
    }
    if (!IsSupported(format)) {
        return std::unexpected(std::string("Unsupported export format"));
    }
    if (signal.Family != WaveformFamily(signal.Config.Modulation)) {
        return std::unexpected(std::string("Result family does not match configuration"));
    }
    const auto family_check = [&] {
        switch (signal.Family) {
        case Family::Noise:
            return ValidateNoise(signal);
        case Family::Fsk:
            return ValidateFsk(signal);
        default:
            return ValidateLinear(signal);
        }
    }();
    if (!family_check.has_value()) {
        return family_check;
    }
    for (const auto sample : signal.Samples) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            return std::unexpected(std::string("Cannot export non-finite samples"));
        }
    }
    return {};
}

const char *DataSourceName(const DataSource source) {
    return source == DataSource::Random ? "Random" : "Explicit";
}

void AppendNoiseSourceMetadata(std::ostream &out, const GenerationConfig &config) {
    out << "  \"noise_source\": {\n    \"noise_power\": " << config.NoiseSource.NoisePower
        << ",\n    \"noise_seed\": " << config.NoiseSeed
        << ",\n    \"rule\": \"Box-Muller from mt19937; open-interval uniforms; I=cos, Q=sin\""
        << "\n  },\n";
}

void AppendFskConfiguration(std::ostream &out, const GenerationConfig &config) {
    out << "  \"configuration\": {\n    \"modulation\": " << JsonQuote(ModulationName(config.Modulation))
        << ",\n    \"symbol_count\": " << config.SymbolCount << ",\n    \"symbol_rate_baud\": " << config.SymbolRateBaud
        << ",\n    \"samples_per_symbol\": " << config.SamplesPerSymbol
        << ",\n    \"tone_spacing_hz\": " << FskToneSpacingHz(config)
        << ",\n    \"modulation_index\": " << FskModulationIndex(config)
        << ",\n    \"amplitude_gain\": " << config.AmplitudeGain << ",\n    \"data_source\": \""
        << DataSourceName(config.DataSource) << '"' << ",\n    \"seed\": " << config.Seed
        << ",\n    \"bits\": " << JsonQuote(config.Bits)
        << ",\n    \"random_bit_rule\": \"mt19937: one output per bit, least significant bit\""
        << ",\n    \"tone_rule\": \"continuous phase from zero; ascending Gray-labelled tones at (m - (M-1)/2) * "
           "tone spacing\"\n  },\n";
}

void AppendLinearConfiguration(std::ostream &out, const GenerationConfig &config) {
    out << "  \"symbol_energy\": 1,\n  \"configuration\": {\n    \"modulation\": "
        << JsonQuote(ModulationName(config.Modulation)) << ",\n    \"symbol_count\": " << config.SymbolCount
        << ",\n    \"symbol_rate_baud\": " << config.SymbolRateBaud
        << ",\n    \"samples_per_symbol\": " << config.SamplesPerSymbol << ",\n    \"pulse\": \""
        << (config.Pulse == Pulse::RRC ? "RRC" : "Rectangular") << '"' << ",\n    \"roll_off\": " << config.RollOff
        << ",\n    \"span_symbols\": " << config.SpanSymbols << ",\n    \"amplitude_gain\": " << config.AmplitudeGain
        << ",\n    \"data_source\": \"" << DataSourceName(config.DataSource) << '"'
        << ",\n    \"seed\": " << config.Seed << ",\n    \"bits\": " << JsonQuote(config.Bits)
        << ",\n    \"random_bit_rule\": \"mt19937: one output per bit, least significant bit\"\n  },\n";
}

// AWGN and impairment provenance, shared by every non-noise family
void AppendNoiseMetadata(std::ostream &out, const GeneratedSignal &signal) {
    const auto &noise = signal.Noise;
    if (noise.AwgnApplied) {
        out << "  \"awgn\": {\n    \"requested_snr_db\": " << noise.RequestedSnrDb
            << ",\n    \"reference_power\": " << noise.ReferencePower
            << ",\n    \"reference_interval\": {\"begin\": " << noise.ReferenceBegin
            << ", \"end\": " << noise.ReferenceEnd << "}"
            << ",\n    \"added_noise_power\": " << noise.AddedNoisePower << ",\n    \"noise_seed\": " << noise.NoiseSeed
            << ",\n    \"snr_definition\": \"clean sample power / added complex noise power\"\n  },\n";
    }
    if (signal.ImpairmentsApplied) {
        out << "  \"impairments\": " << ImpairmentsJson(signal.Config.Impairments, signal.Config.ImpairmentSeed, "  ")
            << ",\n";
    }
}

void WriteLittleEndianFloat(std::ostream &out, const float value) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    const auto word = std::bit_cast<std::uint32_t>(value);
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out.put(static_cast<char>((word >> shift) & 255));
    }
}

// siggen's own metadata document (version 2): the CSV sidecar, and the `siggen:metadata` of a SigMF recording
std::expected<std::string, std::string> SiggenMetadata(const GeneratedSignal &signal, const ExportFormat format) {
    if (const auto validated = ValidateResult(signal, format); !validated.has_value()) {
        return std::unexpected(validated.error());
    }
    const auto &config = signal.Config;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << "{\n  \"version\": 2,\n  \"format\": \""
        << (format == ExportFormat::CSV ? "csv" : "cf32_le")
        << "\",\n  \"waveform\": " << JsonQuote(ModulationName(config.Modulation)) << ",\n  \"family\": \""
        << FamilyName(signal.Family) << "\",\n  \"sample_count\": " << signal.Samples.size()
        << ",\n  \"sample_rate_hz\": " << signal.SampleRateHz
        << ",\n  \"filter_delay_samples\": " << signal.FilterDelaySamples << ",\n  \"scale\": " << config.AmplitudeGain
        << ",\n  \"sample_units\": \"relative amplitude\",\n";
    switch (signal.Family) {
    case Family::Noise:
        AppendNoiseSourceMetadata(out, config);
        break;
    case Family::Fsk:
        AppendFskConfiguration(out, config);
        AppendNoiseMetadata(out, signal);
        break;
    default:
        AppendLinearConfiguration(out, config);
        AppendNoiseMetadata(out, signal);
        break;
    }
    out << "  \"timing\": {\n    \"filter_delay_samples\": " << signal.FilterDelaySamples
        << ",\n    \"duration_s\": " << (signal.SampleRateHz > 0 ? signal.Samples.size() / signal.SampleRateHz : 0)
        << "\n  }\n}\n";
    return out.str();
}

std::string Trimmed(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

} // namespace

/**
 * @brief   Returns the path of the metadata file of an exported sample file.
 * @param[in] destination   Path of the sample file.
 * @return  The `.sigmf-meta` sibling of a `.sigmf-data` file; otherwise the same path with ".json" appended.
 */
std::filesystem::path MetadataPath(const std::filesystem::path &destination) {
    if (destination.extension() == ".sigmf-data") {
        return std::filesystem::path(destination).replace_extension(".sigmf-meta");
    }
    return destination.string() + ".json";
}

/**
 * @brief   Quotes text as a JSON string; minimal escaping shared by export and batch sidecars and manifests.
 * @param[in] text  Raw bytes.
 * @return  The quoted string; quotes and backslashes are escaped, control and non-ASCII bytes become \u00XX.
 */
std::string JsonQuote(const std::string_view text) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char character : text) {
        if (character == '"' || character == '\\') {
            out << '\\' << character;
        } else if (character < 32 || character >= 127) {
            out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(character);
        } else {
            out << character;
        }
    }
    out << '"';
    return out.str();
}

/**
 * @brief   Builds the metadata document of an export.
 * @param[in] signal    Generated signal; must be consistent with its configuration.
 * @param[in] format    Export format.
 * @return  For CSV, siggen's JSON sidecar (version 2). For SigMF, a SigMF 1.0.0 `.sigmf-meta` document whose
 *          `core:` fields describe the recording, with the generator preset under `siggen:preset` and siggen's
 *          sidecar under `siggen:metadata`. Numbers are written with max_digits10 in the classic locale. An error
 *          when the signal or the format cannot be exported.
 */
std::expected<std::string, std::string> ExportMetadata(const GeneratedSignal &signal, const ExportFormat format) {
    if (format != ExportFormat::SigMF) {
        return SiggenMetadata(signal, format);
    }
    const auto detail = SiggenMetadata(signal, ExportFormat::SigMF);
    if (!detail.has_value()) {
        return std::unexpected(detail.error());
    }
    const auto preset = SerializePreset(signal.Config);
    if (!preset.has_value()) {
        return std::unexpected(preset.error());
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << "{\n  \"global\": {\n    \"core:datatype\": \"cf32_le\",\n    \"core:sample_rate\": " << signal.SampleRateHz
        << ",\n    \"core:version\": \"1.0.0\",\n    \"core:description\": "
        << JsonQuote(std::string("Synthetic ") + ModulationName(signal.Config.Modulation) +
                     " baseband signal from siggen")
        << ",\n    \"core:recorder\": \"siggen\",\n    \"siggen:preset\": " << JsonQuote(*preset)
        << ",\n    \"siggen:metadata\": " << Trimmed(*detail)
        << "\n  },\n  \"captures\": [\n    {\"core:sample_start\": 0, \"core:frequency\": 0}\n  ],\n  "
           "\"annotations\": []\n}\n";
    return out.str();
}

/**
 * @brief   Writes raw samples in an export format.
 * @param[out] stream           Destination stream; switched to the classic locale and max_digits10 precision.
 * @param[in]  samples          Samples to write; all must be finite.
 * @param[in]  sample_rate_hz   Sample rate for the CSV time column; must be positive and finite.
 * @param[in]  format           CSV with a "time_s,i,q" header, or SigMF: interleaved float32 little-endian.
 * @return  Nothing on success; an error for a bad format, sample rate or a non-finite sample (samples before it
 *          are already written), or when writing or flushing fails.
 */
std::expected<void, std::string> WriteSamples(std::ostream &stream, const std::span<const std::complex<float>> samples,
                                              const double sample_rate_hz, const ExportFormat format) {
    if (!IsSupported(format)) {
        return std::unexpected(std::string("Unsupported export format"));
    }
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0) {
        return std::unexpected(std::string("Sample rate must be positive and finite"));
    }
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10);
    if (format == ExportFormat::CSV) {
        stream << "time_s,i,q\n";
    }
    for (std::size_t k = 0; k < samples.size(); ++k) {
        if (!std::isfinite(samples[k].real()) || !std::isfinite(samples[k].imag())) {
            return std::unexpected(std::string("Cannot export non-finite samples"));
        }
        if (format == ExportFormat::CSV) {
            stream << k / sample_rate_hz << ',' << samples[k].real() << ',' << samples[k].imag() << '\n';
        } else {
            WriteLittleEndianFloat(stream, samples[k].real());
            WriteLittleEndianFloat(stream, samples[k].imag());
        }
    }
    stream.flush();
    if (!stream) {
        return std::unexpected(std::string("Sample write or flush failed"));
    }
    return {};
}

/**
 * @brief   Writes the samples of a generated signal after checking it is consistent with its configuration.
 * @param[out] stream   Destination stream.
 * @param[in]  signal   Generated signal.
 * @param[in]  format   Sample file format.
 * @return  Nothing on success; an error when the signal cannot be exported, before anything is written.
 */
std::expected<void, std::string> WriteSamples(std::ostream &stream, const GeneratedSignal &signal,
                                              const ExportFormat format) {
    if (const auto validated = ValidateResult(signal, format); !validated.has_value()) {
        return validated;
    }
    return WriteSamples(stream, std::span<const std::complex<float>>(signal.Samples), signal.SampleRateHz, format);
}

/**
 * @brief   Writes a sample file and its metadata file (MetadataPath()).
 * @param[in] destination   Sample file path; must not be empty.
 * @param[in] signal        Generated signal.
 * @param[in] format        Sample file format.
 * @param[in] overwrite     Replaces existing files; otherwise refuses when either file exists.
 * @return  Nothing on success; an error for an invalid signal or an empty path, when a file exists and
 *          @p overwrite is false, or when a file cannot be created or written.
 */
std::expected<void, std::string> ExportSignal(const std::filesystem::path &destination, const GeneratedSignal &signal,
                                              const ExportFormat format, const bool overwrite) {
    const auto metadata = ExportMetadata(signal, format);
    if (!metadata.has_value()) {
        return std::unexpected(metadata.error());
    }
    const auto sidecar = MetadataPath(destination);
    if (destination.empty()) {
        return std::unexpected(std::string("Export destination is empty"));
    }
    std::error_code ec;
    if (!overwrite && (std::filesystem::exists(destination, ec) || std::filesystem::exists(sidecar, ec))) {
        return std::unexpected(std::string("Export destination or metadata already exists; confirm overwrite"));
    }
    // Exclusive creation protects against files appearing after the UI existence check
    const auto mode = std::ios::binary | (overwrite ? std::ios::trunc : std::ios::noreplace);
    std::ofstream data_file, metadata_file;
    const auto cleanup = [&] {
        data_file.close();
        metadata_file.close();
        if (!overwrite) {
            std::filesystem::remove(destination, ec);
            std::filesystem::remove(sidecar, ec);
        }
    };
    data_file.open(destination, mode);
    if (!data_file) {
        return std::unexpected("Cannot create export destination: " + destination.string());
    }
    metadata_file.open(sidecar, mode);
    if (!metadata_file) {
        cleanup();
        return std::unexpected("Cannot create metadata file: " + sidecar.string());
    }
    if (const auto written = WriteSamples(data_file, signal, format); !written.has_value()) {
        cleanup();
        return written;
    }
    metadata_file << *metadata;
    data_file.close();
    metadata_file.close();
    if (data_file.fail() || metadata_file.fail()) {
        if (!overwrite) {
            std::filesystem::remove(destination, ec);
            std::filesystem::remove(sidecar, ec);
        }
        return std::unexpected(std::string("Export write or close failed"));
    }
    return {};
}

} // namespace Core
