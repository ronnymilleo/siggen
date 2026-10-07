#include "iq_export.h"
#include "impairments.h"
#include "preset.h"
#include "signal_processing.h"
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <ostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
namespace iq {
namespace {
bool supported(ExportFormat format) {
    return format == ExportFormat::CSV || format == ExportFormat::BinaryFloat32 || format == ExportFormat::SigMF;
}
void validate_linear(const GeneratedSignal& r) {
    const auto sps = static_cast<std::size_t>(r.config.samples_per_symbol);
    const auto taps = uses_rrc(r.config) ? static_cast<std::size_t>(r.config.span_symbols) * sps + 1 : sps;
    if (r.samples.size() != static_cast<std::size_t>(r.config.symbol_count-1) * sps + taps + quadrature_delay_samples(r.config) ||
        r.symbols.size() != static_cast<std::size_t>(r.config.symbol_count) ||
        r.sample_rate_hz != r.config.symbol_rate_baud * sps ||
        r.filter_delay_samples != (uses_rrc(r.config) ? (taps-1)/2 : 0))
        throw std::invalid_argument("Result metadata does not match configuration");
}
void validate_fsk(const GeneratedSignal& r) {
    const auto sps = static_cast<std::size_t>(r.config.samples_per_symbol);
    if (r.samples.size() != static_cast<std::size_t>(r.config.symbol_count) * sps || !r.symbols.empty() ||
        r.symbol_frequencies_hz.size() != static_cast<std::size_t>(r.config.symbol_count) ||
        r.sample_rate_hz != r.config.symbol_rate_baud * sps || r.filter_delay_samples != 0)
        throw std::invalid_argument("FSK result metadata does not match configuration");
}
void validate_noise(const GeneratedSignal& r) {
    if (r.samples.size() != static_cast<std::size_t>(r.config.noise_source.sample_count) ||
        !r.symbols.empty() || r.sample_rate_hz != r.config.noise_source.sample_rate_hz ||
        r.filter_delay_samples != 0)
        throw std::invalid_argument("Noise result metadata does not match configuration");
}
void validate_result(const GeneratedSignal& r, ExportFormat format) {
    validate(r.config);
    if (!supported(format)) throw std::invalid_argument("Unsupported export format");
    if (r.family != waveform_family(r.config.modulation)) throw std::invalid_argument("Result family does not match configuration");
    if (r.family == Family::Noise) validate_noise(r);
    else if (r.family == Family::Fsk) validate_fsk(r);
    else validate_linear(r);
    for (auto value : r.samples)
        if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) throw std::invalid_argument("Cannot export non-finite samples");
}
// AWGN and impairment provenance, shared by every non-noise family.
void append_noise_metadata(std::ostream& out, const GeneratedSignal& r) {
    if (r.noise.awgn_applied) {
        out << "  \"awgn\": {\n    \"requested_snr_db\": " << r.noise.requested_snr_db
            << ",\n    \"reference_power\": " << r.noise.reference_power
            << ",\n    \"reference_interval\": {\"begin\": " << r.noise.reference_begin << ", \"end\": " << r.noise.reference_end << "}"
            << ",\n    \"added_noise_power\": " << r.noise.added_noise_power
            << ",\n    \"noise_seed\": " << r.noise.noise_seed
            << ",\n    \"snr_definition\": \"clean sample power / added complex noise power\"\n  },\n";
    }
    if (r.impairments_applied)
        out << "  \"impairments\": " << impairments_json(r.config.impairments, r.config.impairment_seed, "  ") << ",\n";
}
std::string quote(std::string_view text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32 || c >= 127) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c);
        else out << c;
    }
    out << '"'; return out.str();
}
void little_float(std::ostream& out, float value) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    const auto word = std::bit_cast<std::uint32_t>(value);
    for (unsigned shift = 0; shift < 32; shift += 8) out.put(static_cast<char>((word >> shift) & 255));
}
}
std::string json_quote(std::string_view text) { return quote(text); }
std::filesystem::path metadata_path(const std::filesystem::path& destination) {
    if (destination.extension() == ".sigmf-data") return std::filesystem::path(destination).replace_extension(".sigmf-meta");
    return destination.string() + ".json";
}
namespace {
std::string siggen_metadata(const GeneratedSignal& r, ExportFormat format) {
    validate_result(r, format);
    const auto& c = r.config;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << "{\n  \"version\": 2,\n  \"format\": \"" << (format == ExportFormat::CSV ? "csv" : "cf32_le")
        << "\",\n  \"waveform\": " << quote(modulation_name(c.modulation))
        << ",\n  \"family\": \"" << family_name(r.family)
        << "\",\n  \"sample_count\": " << r.samples.size() << ",\n  \"sample_rate_hz\": " << r.sample_rate_hz
        << ",\n  \"filter_delay_samples\": " << r.filter_delay_samples
        << ",\n  \"scale\": " << c.amplitude_gain << ",\n  \"sample_units\": \"relative amplitude\",\n";
    if (r.family == Family::Noise) {
        out << "  \"noise_source\": {\n    \"noise_power\": " << c.noise_source.noise_power
            << ",\n    \"noise_seed\": " << c.noise_seed
            << ",\n    \"rule\": \"Box-Muller from mt19937; open-interval uniforms; I=cos, Q=sin\""
            << "\n  },\n";
    } else if (r.family == Family::Fsk) {
        out << "  \"configuration\": {\n    \"modulation\": " << quote(modulation_name(c.modulation))
            << ",\n    \"symbol_count\": " << c.symbol_count << ",\n    \"symbol_rate_baud\": " << c.symbol_rate_baud
            << ",\n    \"samples_per_symbol\": " << c.samples_per_symbol
            << ",\n    \"tone_spacing_hz\": " << fsk_tone_spacing_hz(c)
            << ",\n    \"modulation_index\": " << fsk_modulation_index(c)
            << ",\n    \"amplitude_gain\": " << c.amplitude_gain
            << ",\n    \"data_source\": \"" << (c.data_source == DataSource::Random ? "Random" : "Explicit") << '"'
            << ",\n    \"seed\": " << c.seed << ",\n    \"bits\": " << quote(c.bits)
            << ",\n    \"random_bit_rule\": \"mt19937: one output per bit, least significant bit\""
            << ",\n    \"tone_rule\": \"continuous phase from zero; ascending Gray-labelled tones at (m - (M-1)/2) * tone spacing\"\n  },\n";
        append_noise_metadata(out, r);
    } else {
        out << "  \"symbol_energy\": 1,\n  \"configuration\": {\n    \"modulation\": " << quote(modulation_name(c.modulation))
            << ",\n    \"symbol_count\": " << c.symbol_count << ",\n    \"symbol_rate_baud\": " << c.symbol_rate_baud
            << ",\n    \"samples_per_symbol\": " << c.samples_per_symbol
            << ",\n    \"pulse\": \"" << (c.pulse == Pulse::RRC ? "RRC" : "Rectangular") << '"'
            << ",\n    \"roll_off\": " << c.roll_off << ",\n    \"span_symbols\": " << c.span_symbols
            << ",\n    \"amplitude_gain\": " << c.amplitude_gain
            << ",\n    \"data_source\": \"" << (c.data_source == DataSource::Random ? "Random" : "Explicit") << '"'
            << ",\n    \"seed\": " << c.seed << ",\n    \"bits\": " << quote(c.bits)
            << ",\n    \"random_bit_rule\": \"mt19937: one output per bit, least significant bit\"\n  },\n";
        append_noise_metadata(out, r);
    }
    out << "  \"timing\": {\n    \"filter_delay_samples\": " << r.filter_delay_samples
        << ",\n    \"duration_s\": " << (r.sample_rate_hz > 0 ? r.samples.size() / r.sample_rate_hz : 0)
        << "\n  }\n}\n";
    return out.str();
}
std::string trimmed(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    return text;
}
}
std::string export_metadata(const GeneratedSignal& r, ExportFormat format) {
    if (format != ExportFormat::SigMF) return siggen_metadata(r, format);
    // SigMF 1.0.0 core fields; everything siggen-specific lives under the `siggen:` namespace.
    const auto detail = siggen_metadata(r, ExportFormat::BinaryFloat32);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << "{\n  \"global\": {\n    \"core:datatype\": \"cf32_le\",\n    \"core:sample_rate\": " << r.sample_rate_hz
        << ",\n    \"core:version\": \"1.0.0\",\n    \"core:description\": " << quote(std::string("Synthetic ") + modulation_name(r.config.modulation) + " baseband signal from siggen")
        << ",\n    \"core:recorder\": \"siggen\",\n    \"siggen:preset\": " << quote(serialize_preset(r.config))
        << ",\n    \"siggen:metadata\": " << trimmed(detail)
        << "\n  },\n  \"captures\": [\n    {\"core:sample_start\": 0, \"core:frequency\": 0}\n  ],\n  \"annotations\": []\n}\n";
    return out.str();
}
void write_samples(std::ostream& out, std::span<const std::complex<float>> samples, double sample_rate_hz, ExportFormat format) {
    if (!supported(format)) throw std::invalid_argument("Unsupported export format");
    if (!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0) throw std::invalid_argument("Sample rate must be positive and finite");
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    if (format == ExportFormat::CSV) out << "time_s,i,q\n";
    for (std::size_t k = 0; k < samples.size(); ++k) {
        if (!std::isfinite(samples[k].real()) || !std::isfinite(samples[k].imag()))
            throw std::invalid_argument("Cannot export non-finite samples");
        if (format == ExportFormat::CSV)
            out << k / sample_rate_hz << ',' << samples[k].real() << ',' << samples[k].imag() << '\n';
        else { little_float(out, samples[k].real()); little_float(out, samples[k].imag()); }
    }
    out.flush();
    if (!out) throw std::runtime_error("Sample write or flush failed");
}
void write_samples(std::ostream& out, const GeneratedSignal& r, ExportFormat format) {
    validate_result(r, format);
    write_samples(out, std::span<const std::complex<float>>(r.samples), r.sample_rate_hz, format);
}
void export_signal(const std::filesystem::path& path, const GeneratedSignal& r, ExportFormat format, bool overwrite) {
    const auto metadata = export_metadata(r, format);
    const auto sidecar = metadata_path(path);
    if (path.empty()) throw std::invalid_argument("Export destination is empty");
    if (!overwrite && (std::filesystem::exists(path) || std::filesystem::exists(sidecar)))
        throw std::runtime_error("Export destination or metadata already exists; confirm overwrite");
    // Exclusive creation protects against files appearing after the UI existence check.
    const auto mode = std::ios::binary | (overwrite ? std::ios::trunc : std::ios::noreplace);
    std::ofstream data, meta;
    data.exceptions(std::ios::badbit | std::ios::failbit);
    meta.exceptions(std::ios::badbit | std::ios::failbit);
    data.open(path, mode);
    meta.open(sidecar, mode);
    write_samples(data, r, format);
    meta << metadata;
    data.close(); meta.close();
}
}
