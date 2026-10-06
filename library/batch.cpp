#include "batch.h"
#include "impairments.h"
#include "noise.h"
#include "signal_processing.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace iq {
namespace {
// Fixed stream tags for seed derivation; documented in batch.h.
constexpr std::uint32_t STREAM_TAG_DATA = 1;
constexpr std::uint32_t STREAM_TAG_NOISE = 2;
constexpr std::uint32_t STREAM_TAG_IMPAIRMENT = 3;
std::uint32_t derive_seed(std::initializer_list<std::uint32_t> material) {
    std::seed_seq sequence(material.begin(), material.end());
    std::mt19937 engine(sequence);
    return engine();
}
std::vector<Modulation> effective_waveforms(const BatchRequest& r) {
    return r.waveforms.empty() ? std::vector<Modulation>{r.base.modulation} : r.waveforms;
}
std::vector<std::uint32_t> effective_seeds(const BatchRequest& r) {
    return r.seeds.empty() ? std::vector<std::uint32_t>{r.base.seed} : r.seeds;
}
const char* format_name(ExportFormat format) {
    return format == ExportFormat::CSV ? "csv" : "cf32_le";
}
const char* format_extension(ExportFormat format) {
    return format == ExportFormat::CSV ? ".csv" : ".cf32";
}
std::string frame_basename(std::size_t point_index, std::size_t frame_index) {
    std::ostringstream out;
    out << "frame_" << std::setw(6) << std::setfill('0') << point_index << '_'
        << std::setw(6) << std::setfill('0') << frame_index;
    return out.str();
}
std::ostringstream json_stream() {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    return out;
}
}
std::uint32_t derive_data_seed(std::uint32_t base_seed, Modulation waveform, std::size_t frame_index) {
    return derive_seed({base_seed, static_cast<std::uint32_t>(waveform_id(waveform)),
                        static_cast<std::uint32_t>(frame_index), STREAM_TAG_DATA});
}
std::uint32_t derive_noise_seed(std::uint32_t base_seed, Modulation waveform, std::size_t frame_index,
                                std::uint32_t configured_noise_seed) {
    return derive_seed({base_seed, static_cast<std::uint32_t>(waveform_id(waveform)),
                        static_cast<std::uint32_t>(frame_index), STREAM_TAG_NOISE, configured_noise_seed});
}
std::uint32_t derive_impairment_seed(std::uint32_t base_seed, Modulation waveform, std::size_t frame_index,
                                     std::uint32_t configured_impairment_seed) {
    return derive_seed({base_seed, static_cast<std::uint32_t>(waveform_id(waveform)),
                        static_cast<std::uint32_t>(frame_index), STREAM_TAG_IMPAIRMENT, configured_impairment_seed});
}
void validate_frame_request(const GenerationConfig& base, Modulation waveform, int frame_size) {
    if (!is_valid(waveform)) throw std::invalid_argument("Unsupported modulation");
    if (frame_size < 1 || static_cast<std::size_t>(frame_size) > MAX_SIGNAL_SAMPLES)
        throw std::invalid_argument("Frame size must be 1–4194304 samples");
    auto config = base;
    config.modulation = waveform;
    config.data_source = DataSource::Random;
    config.bits.clear();
    if (waveform_family(waveform) == Family::Noise) {
        config.noise_source.sample_count = frame_size;
        // WGN ignores the SNR axis entirely; any base AWGN setting is inactive.
        config.awgn.enabled = false;
        config.impairments = {};
        validate(config);
        return;
    }
    // Validate pulse/rate/gain ranges with a trivial length before frame arithmetic.
    config.symbol_count = 1;
    validate(config);
    const auto sps = static_cast<std::size_t>(config.samples_per_symbol);
    const auto payload = (static_cast<std::size_t>(frame_size) + sps - 1) / sps;
    const auto guard = config.pulse == Pulse::RRC ? static_cast<std::size_t>(config.span_symbols) : 0;
    const auto symbols = payload + 2 * guard;
    if (symbols > 65536) throw std::length_error("Frame generation exceeds symbol limit including guards");
    config.symbol_count = static_cast<int>(symbols);
    validate(config);
    const auto taps = config.pulse == Pulse::RRC
                          ? static_cast<std::size_t>(config.span_symbols) * sps + 1 : sps;
    const auto full_size = (symbols - 1) * sps + taps;
    const auto offset = guard * sps + (config.pulse == Pulse::RRC ? (taps - 1) / 2 : 0);
    if (offset + static_cast<std::size_t>(frame_size) > full_size)
        throw std::length_error("Frame crop exceeds generated buffer");
}
FrameResult generate_frame(const GenerationConfig& base, int frame_size, std::size_t frame_index,
                           const std::optional<double>& snr_db) {
    validate_frame_request(base, base.modulation, frame_size);
    FrameResult frame;
    frame.data_seed = derive_data_seed(base.seed, base.modulation, frame_index);
    frame.noise_seed = derive_noise_seed(base.seed, base.modulation, frame_index, base.noise_seed);
    std::optional<double> effective_snr = snr_db;
    if (!effective_snr && base.awgn.enabled) effective_snr = base.awgn.snr_db;
    auto config = base;
    config.data_source = DataSource::Random;
    config.bits.clear();
    config.awgn.enabled = false;
    config.impairments = {};
    config.noise_seed = frame.noise_seed;
    if (waveform_family(base.modulation) == Family::Noise) {
        config.noise_source.sample_count = frame_size;
        auto full = generate(config);
        frame.samples = std::move(full.samples);
        frame.sample_rate_hz = full.sample_rate_hz;
        frame.noise = NoiseRecord{false, 0, 0, 0, 0, config.noise_source.noise_power, frame.noise_seed};
        return frame;
    }
    config.seed = frame.data_seed;
    const auto sps = static_cast<std::size_t>(config.samples_per_symbol);
    const auto payload = (static_cast<std::size_t>(frame_size) + sps - 1) / sps;
    const auto guard = config.pulse == Pulse::RRC ? static_cast<std::size_t>(config.span_symbols) : 0;
    config.symbol_count = static_cast<int>(payload + 2 * guard);
    auto full = generate(config);
    frame.crop_offset = guard * sps + full.filter_delay_samples;
    frame.filter_delay_samples = full.filter_delay_samples;
    frame.sample_rate_hz = full.sample_rate_hz;
    frame.samples.assign(full.samples.begin() + static_cast<std::ptrdiff_t>(frame.crop_offset),
                         full.samples.begin() + static_cast<std::ptrdiff_t>(frame.crop_offset + static_cast<std::size_t>(frame_size)));
    if (effective_snr) {
        // Batch AWGN measures reference power over the retained clean frame.
        frame.noise = add_awgn(frame.samples, 0, frame.samples.size(), *effective_snr, frame.noise_seed);
    }
    if (base.impairments.active()) {
        frame.impairment_seed = derive_impairment_seed(base.seed, base.modulation, frame_index, base.impairment_seed);
        apply_impairments(frame.samples, frame.sample_rate_hz, base.impairments, frame.impairment_seed);
        frame.impairments_applied = true;
    }
    return frame;
}
void validate_batch(const BatchRequest& r) {
    if (r.frames_per_point < 1) throw std::invalid_argument("Frames per point must be at least 1");
    if (r.format != ExportFormat::CSV && r.format != ExportFormat::BinaryFloat32)
        throw std::invalid_argument("Unsupported batch format");
    if (r.output_dir.empty()) throw std::invalid_argument("Batch output directory is empty");
    if (r.base.data_source == DataSource::Explicit)
        throw std::invalid_argument("Batch generation requires seeded random data");
    if (std::filesystem::exists(r.output_dir))
        throw std::runtime_error("Batch output directory already exists: " + r.output_dir.string());
    const auto waveforms = effective_waveforms(r);
    const auto seeds = effective_seeds(r);
    std::set<int> waveform_ids;
    for (auto waveform : waveforms) {
        if (!is_valid(waveform)) throw std::invalid_argument("Unsupported modulation");
        if (!waveform_ids.insert(waveform_id(waveform)).second)
            throw std::invalid_argument(std::string("Duplicate waveform in sweep: ") + modulation_name(waveform));
    }
    std::set<std::uint32_t> unique_seeds;
    for (auto seed : seeds)
        if (!unique_seeds.insert(seed).second)
            throw std::invalid_argument("Duplicate seed in sweep: " + std::to_string(seed));
    std::set<double> unique_snrs;
    for (auto snr : r.snrs_db) {
        if (!std::isfinite(snr)) throw std::invalid_argument("SNR values must be finite");
        if (!unique_snrs.insert(snr).second)
            throw std::invalid_argument("Duplicate SNR in sweep: " + std::to_string(snr));
    }
    std::size_t points = 0;
    for (auto waveform : waveforms) {
        validate_frame_request(r.base, waveform, r.frame_size);
        if (waveform_descriptor(waveform).awgn_supported &&
            (!r.snrs_db.empty() || r.base.awgn.enabled)) {
            if (r.base.amplitude_gain == 0)
                throw std::invalid_argument("AWGN reference power must be positive");
            if (r.snrs_db.empty()) snr_power_ratio(r.base.awgn.snr_db);
            else for (auto snr : r.snrs_db) snr_power_ratio(snr);
        }
        // WGN ignores the SNR axis: one point per seed at the configured power.
        const auto per_seed = (waveform_family(waveform) == Family::Noise || r.snrs_db.empty())
                                  ? 1 : r.snrs_db.size();
        if (seeds.size() > (MAX_BATCH_FRAMES - points) / per_seed)
            throw std::length_error("Batch exceeds frame limit");
        points += seeds.size() * per_seed;
    }
    if (r.frames_per_point > static_cast<int>(MAX_BATCH_FRAMES / points))
        throw std::length_error("Batch exceeds frame limit");
}
namespace {
std::string frame_sidecar(const BatchRequest& r, Modulation waveform, std::uint32_t seed,
                          const std::optional<double>& snr, std::size_t point_index, std::size_t frame_index,
                          const FrameResult& frame, const std::string& data_name) {
    auto out = json_stream();
    const bool noise = waveform_family(waveform) == Family::Noise;
    out << "{\n  \"version\": 2,\n  \"kind\": \"batch_frame\",\n  \"format\": \"" << format_name(r.format)
        << "\",\n  \"waveform\": " << json_quote(modulation_name(waveform))
        << ",\n  \"family\": \"" << (noise ? "noise" : "linear")
        << "\",\n  \"frame_size\": " << frame.samples.size()
        << ",\n  \"sample_rate_hz\": " << frame.sample_rate_hz
        << ",\n  \"sample_units\": \"relative amplitude\",\n  \"amplitude_gain\": " << r.base.amplitude_gain
        << ",\n  \"point_index\": " << point_index << ",\n  \"frame_index\": " << frame_index
        << ",\n  \"data_file\": " << json_quote(data_name)
        << ",\n  \"axes\": {\n    \"seed\": " << seed << ",\n    \"snr_db\": ";
    if (snr) out << *snr; else out << "null";
    out << "\n  },\n  \"derived_seeds\": {\n    \"data\": " << frame.data_seed
        << ",\n    \"noise\": " << frame.noise_seed << "\n  },\n";
    if (noise) {
        out << "  \"noise_source\": {\n    \"noise_power\": " << r.base.noise_source.noise_power
            << ",\n    \"rule\": \"Box-Muller from mt19937; open-interval uniforms; I=cos, Q=sin\"\n  },\n";
    } else {
        out << "  \"linear\": {\n    \"pulse\": \"" << (r.base.pulse == Pulse::RRC ? "RRC" : "Rectangular")
            << "\",\n    \"roll_off\": " << r.base.roll_off << ",\n    \"span_symbols\": " << r.base.span_symbols
            << ",\n    \"samples_per_symbol\": " << r.base.samples_per_symbol
            << ",\n    \"symbol_rate_baud\": " << r.base.symbol_rate_baud << "\n  },\n"
            << "  \"crop\": {\n    \"offset_samples\": " << frame.crop_offset
            << ",\n    \"filter_delay_samples\": " << frame.filter_delay_samples
            << ",\n    \"guard_symbols\": " << (r.base.pulse == Pulse::RRC ? r.base.span_symbols : 0)
            << "\n  },\n";
        if (frame.noise.awgn_applied) {
            out << "  \"awgn\": {\n    \"requested_snr_db\": " << frame.noise.requested_snr_db
                << ",\n    \"reference_power\": " << frame.noise.reference_power
                << ",\n    \"reference_interval\": {\"begin\": " << frame.noise.reference_begin << ", \"end\": " << frame.noise.reference_end << "}"
                << ",\n    \"added_noise_power\": " << frame.noise.added_noise_power
                << ",\n    \"noise_seed\": " << frame.noise.noise_seed
                << ",\n    \"snr_definition\": \"clean frame power / added complex noise power\"\n  },\n";
        }
        if (frame.impairments_applied)
            out << "  \"impairments\": " << impairments_json(r.base.impairments, frame.impairment_seed, "  ") << ",\n";
    }
    out << "  \"timing\": {\n    \"frame_start_s\": 0,\n    \"duration_s\": "
        << (frame.sample_rate_hz > 0 ? frame.samples.size() / frame.sample_rate_hz : 0) << "\n  }\n}\n";
    return out.str();
}
std::string manifest_record(const BatchRequest& r, Modulation waveform, std::uint32_t seed,
                            const std::optional<double>& snr, std::size_t point_index, std::size_t frame_index,
                            const FrameResult& frame, const std::string& data_name) {
    auto out = json_stream();
    out << "{\"kind\":\"frame\",\"point_index\":" << point_index << ",\"frame_index\":" << frame_index
        << ",\"path\":" << json_quote(data_name)
        << ",\"sidecar\":" << json_quote(data_name + ".json")
        << ",\"waveform\":" << json_quote(modulation_name(waveform))
        << ",\"family\":\"" << (waveform_family(waveform) == Family::Noise ? "noise" : "linear")
        << "\",\"seed\":" << seed << ",\"snr_db\":";
    if (snr) out << *snr; else out << "null";
    out << ",\"data_seed\":" << frame.data_seed << ",\"noise_seed\":" << frame.noise_seed
        << ",\"frame_size\":" << frame.samples.size()
        << ",\"format\":\"" << format_name(r.format) << '"'
        << ",\"crop_offset_samples\":" << frame.crop_offset
        << ",\"filter_delay_samples\":" << frame.filter_delay_samples
        << ",\"awgn_applied\":" << (frame.noise.awgn_applied ? "true" : "false");
    if (frame.impairments_applied)
        out << ",\"impairment_seed\":" << frame.impairment_seed << ",\"cfo_hz\":" << r.base.impairments.cfo_hz
            << ",\"phase_noise_linewidth_hz\":" << r.base.impairments.phase_noise_linewidth_hz
            << ",\"iq_gain_db\":" << r.base.impairments.iq_gain_db
            << ",\"iq_phase_deg\":" << r.base.impairments.iq_phase_deg
            << ",\"dc_offset_i\":" << r.base.impairments.dc_offset_i
            << ",\"dc_offset_q\":" << r.base.impairments.dc_offset_q
            << ",\"adc_bits\":" << r.base.impairments.adc_bits;
    if (frame.noise.awgn_applied)
        out << ",\"requested_snr_db\":" << frame.noise.requested_snr_db
            << ",\"reference_power\":" << frame.noise.reference_power
            << ",\"added_noise_power\":" << frame.noise.added_noise_power;
    if (waveform_family(waveform) == Family::Noise)
        out << ",\"noise_power\":" << frame.noise.added_noise_power;
    out << "}\n";
    return out.str();
}
std::string manifest_header(const BatchRequest& r, std::size_t point_count, std::size_t frame_count) {
    auto out = json_stream();
    out << "{\"kind\":\"batch_header\",\"manifest_version\":1,\"tool\":\"siggen\",\"format\":\""
        << format_name(r.format) << "\",\"frame_size\":" << r.frame_size
        << ",\"frames_per_point\":" << r.frames_per_point
        << ",\"point_count\":" << point_count << ",\"frame_count\":" << frame_count
        << ",\"waveforms\":[";
    const auto waveforms = effective_waveforms(r);
    for (std::size_t k = 0; k < waveforms.size(); ++k) {
        if (k) out << ',';
        out << json_quote(modulation_name(waveforms[k]));
    }
    out << "],\"seeds\":[";
    const auto seeds = effective_seeds(r);
    for (std::size_t k = 0; k < seeds.size(); ++k) {
        if (k) out << ',';
        out << seeds[k];
    }
    out << "],\"snrs_db\":";
    if (r.snrs_db.empty()) out << "null";
    else {
        out << '[';
        for (std::size_t k = 0; k < r.snrs_db.size(); ++k) {
            if (k) out << ',';
            out << r.snrs_db[k];
        }
        out << ']';
    }
    out << ",\"amplitude_gain\":" << r.base.amplitude_gain << ",\"noise_seed\":" << r.base.noise_seed << "}\n";
    return out.str();
}
}
BatchSummary run_batch(const BatchRequest& r) {
    validate_batch(r);
    if (!std::filesystem::create_directory(r.output_dir))
        throw std::runtime_error("Batch output directory already exists: " + r.output_dir.string());
    const auto waveforms = effective_waveforms(r);
    const auto seeds = effective_seeds(r);
    // Total counts are known up front; the header advertises the full sweep.
    std::size_t point_count = 0;
    for (auto waveform : waveforms)
        point_count += seeds.size() * ((waveform_family(waveform) == Family::Noise || r.snrs_db.empty())
                                           ? 1 : r.snrs_db.size());
    const auto frame_count = point_count * static_cast<std::size_t>(r.frames_per_point);
    std::ofstream manifest(r.output_dir / "manifest.jsonl", std::ios::binary | std::ios::noreplace);
    manifest.exceptions(std::ios::badbit | std::ios::failbit);
    manifest << manifest_header(r, point_count, frame_count);
    manifest.flush();
    BatchSummary summary;
    std::size_t point_index = 0;
    for (auto waveform : waveforms) {
        const bool noise = waveform_family(waveform) == Family::Noise;
        std::vector<std::optional<double>> snr_points{std::nullopt};
        if (!noise && !r.snrs_db.empty()) {
            snr_points.clear();
            for (auto snr : r.snrs_db) snr_points.push_back(snr);
        }
        for (auto seed : seeds) {
            for (const auto& snr : snr_points) {
                auto config = r.base;
                config.modulation = waveform;
                config.seed = seed;
                for (auto frame_index = 0; frame_index < r.frames_per_point; ++frame_index) {
                    const auto frame = generate_frame(config, r.frame_size, static_cast<std::size_t>(frame_index),
                                                      noise ? std::nullopt : snr);
                    const auto name = frame_basename(point_index, static_cast<std::size_t>(frame_index));
                    const auto data_name = name + format_extension(r.format);
                    const auto data_path = r.output_dir / data_name;
                    const auto sidecar_path = r.output_dir / (data_name + ".json");
                    const auto sidecar = frame_sidecar(r, waveform, seed, noise ? std::nullopt : snr,
                                                       point_index, static_cast<std::size_t>(frame_index),
                                                       frame, data_name);
                    {
                        std::ofstream data(data_path, std::ios::binary | std::ios::noreplace);
                        std::ofstream meta(sidecar_path, std::ios::binary | std::ios::noreplace);
                        data.exceptions(std::ios::badbit | std::ios::failbit);
                        meta.exceptions(std::ios::badbit | std::ios::failbit);
                        write_samples(data, std::span<const std::complex<float>>(frame.samples),
                                      frame.sample_rate_hz, r.format);
                        meta << sidecar;
                        data.close();
                        meta.close();
                    }
                    // Completion records are appended only after the frame files closed successfully.
                    manifest << manifest_record(r, waveform, seed, noise ? std::nullopt : snr, point_index,
                                                static_cast<std::size_t>(frame_index), frame, data_name);
                    manifest.flush();
                    ++summary.frame_count;
                }
                ++point_index;
                ++summary.point_count;
            }
        }
    }
    auto summary_line = json_stream();
    summary_line << "{\"kind\":\"summary\",\"completed\":true,\"point_count\":" << summary.point_count
                 << ",\"frame_count\":" << summary.frame_count << "}\n";
    manifest << summary_line.str();
    manifest.flush();
    manifest.close();
    return summary;
}
}
