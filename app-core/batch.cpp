/**
 * @file    batch.cpp
 * @brief   Generates sweeps of fixed-length frames over waveforms, seeds and SNRs into a dataset directory.
 * @details The output directory holds one sample file and one JSON metadata file per frame (a SigMF `.sigmf-meta`
 *          for SigMF frames), plus manifest.jsonl: a header record, one record per completed frame and a final
 *          summary record.
 */

#include "batch.h"

#include "impairments.h"
#include "noise.h"
#include "preset.h"
#include "signal_processing.h"
#include <cmath>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <limits>
#include <random>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace Core {

namespace {

// Fixed stream tags for seed derivation
constexpr std::uint32_t StreamTagData = 1;
constexpr std::uint32_t StreamTagNoise = 2;
constexpr std::uint32_t StreamTagImpairment = 3;

constexpr std::size_t MaxFrameSymbols = 65536;

/**
 * @struct  FrameLocation
 * @brief   Where a frame sits in the sweep: its axis values and its indices.
 */
struct FrameLocation {
    Modulation Waveform{};
    std::uint32_t Seed = 0;
    std::optional<double> SnrDb; // Empty when the base noise setting applies, and always for noise sources
    std::size_t PointIndex = 0;
    std::size_t FrameIndex = 0;
};

// region Frames

std::uint32_t DeriveSeed(std::initializer_list<std::uint32_t> material) {
    std::seed_seq sequence(material.begin(), material.end());
    std::mt19937 engine(sequence);
    return engine();
}

std::size_t PayloadSymbols(const int frame_size, const std::size_t samples_per_symbol) {
    return (static_cast<std::size_t>(frame_size) + samples_per_symbol - 1) / samples_per_symbol;
}

// RRC frames carry one filter span of extra symbols on each side, so the crop avoids the filter transients
std::size_t GuardSymbols(const GenerationConfig &config) {
    return UsesRrc(config) ? static_cast<std::size_t>(config.SpanSymbols) : 0;
}

FrameResult GenerateNoiseFrame(GenerationConfig &config, const int frame_size, FrameResult frame) {
    config.NoiseSource.SampleCount = frame_size;
    auto full = Generate(config);
    frame.Samples = std::move(full.Samples);
    frame.SampleRateHz = full.SampleRateHz;
    frame.Noise = NoiseRecord{false, 0, 0, 0, 0, config.NoiseSource.NoisePower, frame.NoiseSeed};
    return frame;
}

/**
 * @brief   Generates a symbol frame with guard symbols and crops it to the requested size.
 * @param[in,out] config        Clean configuration of the frame; receives the data seed and the symbol count.
 * @param[in]     frame_size    Samples in the frame.
 * @param[in]     frame         Frame with its derived seeds set.
 * @return  The cropped clean frame, without AWGN or impairments.
 */
FrameResult GenerateSymbolFrame(GenerationConfig &config, const int frame_size, FrameResult frame) {
    config.Seed = frame.DataSeed;
    const auto samples_per_symbol = static_cast<std::size_t>(config.SamplesPerSymbol);
    const auto guard = GuardSymbols(config);
    config.SymbolCount = static_cast<int>(PayloadSymbols(frame_size, samples_per_symbol) + 2 * guard);
    auto full = Generate(config);
    frame.GeneratorPreset = SerializePreset(config);
    frame.CropOffset = guard * samples_per_symbol + full.FilterDelaySamples;
    frame.FilterDelaySamples = full.FilterDelaySamples;
    frame.SampleRateHz = full.SampleRateHz;
    frame.Samples.assign(full.Samples.begin() + static_cast<std::ptrdiff_t>(frame.CropOffset),
                         full.Samples.begin() +
                             static_cast<std::ptrdiff_t>(frame.CropOffset + static_cast<std::size_t>(frame_size)));
    return frame;
}

// endregion

// region Batch validation

std::vector<Modulation> EffectiveWaveforms(const BatchRequest &request) {
    return request.Waveforms.empty() ? std::vector<Modulation>{request.Base.Modulation} : request.Waveforms;
}

std::vector<std::uint32_t> EffectiveSeeds(const BatchRequest &request) {
    return request.Seeds.empty() ? std::vector<std::uint32_t>{request.Base.Seed} : request.Seeds;
}

// WGN ignores the SNR axis: one point per seed at the configured power
std::size_t PointsPerSeed(const Modulation waveform, const BatchRequest &request) {
    return (WaveformFamily(waveform) == Family::Noise || request.SnrsDb.empty()) ? 1 : request.SnrsDb.size();
}

void ValidateBatchSettings(const BatchRequest &request) {
    if (request.FramesPerPoint < 1) {
        throw std::invalid_argument("Frames per point must be at least 1");
    }
    if (request.Format != ExportFormat::CSV && request.Format != ExportFormat::SigMF) {
        throw std::invalid_argument("Unsupported batch format");
    }
    if (request.OutputDir.empty()) {
        throw std::invalid_argument("Batch output directory is empty");
    }
    if (request.Base.DataSource == DataSource::Explicit) {
        throw std::invalid_argument("Batch generation requires seeded random data");
    }
    if (std::filesystem::exists(request.OutputDir)) {
        throw std::runtime_error("Batch output directory already exists: " + request.OutputDir.string());
    }
}

void ValidateSweepAxes(const std::vector<Modulation> &waveforms, const std::vector<std::uint32_t> &seeds,
                       const std::vector<double> &snrs_db) {
    std::set<int> waveform_ids;
    for (const auto waveform : waveforms) {
        if (!IsValid(waveform)) {
            throw std::invalid_argument("Unsupported modulation");
        }
        if (!waveform_ids.insert(WaveformId(waveform)).second) {
            throw std::invalid_argument(std::string("Duplicate waveform in sweep: ") + ModulationName(waveform));
        }
    }
    std::set<std::uint32_t> unique_seeds;
    for (const auto seed : seeds) {
        if (!unique_seeds.insert(seed).second) {
            throw std::invalid_argument("Duplicate seed in sweep: " + std::to_string(seed));
        }
    }
    std::set<double> unique_snrs;
    for (const auto snr : snrs_db) {
        if (!std::isfinite(snr)) {
            throw std::invalid_argument("SNR values must be finite");
        }
        if (!unique_snrs.insert(snr).second) {
            throw std::invalid_argument("Duplicate SNR in sweep: " + std::to_string(snr));
        }
    }
}

// SnrPowerRatio() throws for SNRs whose power ratio is out of range
void ValidateAwgnSettings(const Modulation waveform, const BatchRequest &request) {
    if (!GetWaveformDescriptor(waveform).AwgnSupported || (request.SnrsDb.empty() && !request.Base.Awgn.Enabled)) {
        return;
    }
    if (request.Base.AmplitudeGain == 0) {
        throw std::invalid_argument("AWGN reference power must be positive");
    }
    if (request.SnrsDb.empty()) {
        SnrPowerRatio(request.Base.Awgn.SnrDb);
    } else {
        for (const auto snr : request.SnrsDb) {
            SnrPowerRatio(snr);
        }
    }
}

// endregion

// region Output records

// Manifest format: how a frame is stored. Sidecars describe the sample encoding instead
const char *FormatName(const ExportFormat format) {
    return format == ExportFormat::CSV ? "csv" : "sigmf";
}

const char *DataFormatName(const ExportFormat format) {
    return format == ExportFormat::CSV ? "csv" : "cf32_le";
}

const char *FormatExtension(const ExportFormat format) {
    return format == ExportFormat::CSV ? ".csv" : ".sigmf-data";
}

std::string FrameBasename(const std::size_t point_index, const std::size_t frame_index) {
    std::ostringstream out;
    out << "frame_" << std::setw(6) << std::setfill('0') << point_index << '_' << std::setw(6) << std::setfill('0')
        << frame_index;
    return out.str();
}

std::ostringstream JsonStream() {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    return out;
}

void AppendOptionalNumber(std::ostream &out, const std::optional<double> &value) {
    if (value) {
        out << *value;
    } else {
        out << "null";
    }
}

template <class T, class Writer> void AppendJsonArray(std::ostream &out, const std::vector<T> &values, Writer write) {
    out << '[';
    for (std::size_t k = 0; k < values.size(); ++k) {
        if (k) {
            out << ',';
        }
        write(values[k]);
    }
    out << ']';
}

void AppendNoiseSourceSidecar(std::ostream &out, const BatchRequest &request) {
    out << "  \"noise_source\": {\n    \"noise_power\": " << request.Base.NoiseSource.NoisePower
        << ",\n    \"rule\": \"Box-Muller from mt19937; open-interval uniforms; I=cos, Q=sin\"\n  },\n";
}

void AppendFskSidecar(std::ostream &out, const BatchRequest &request, const Modulation waveform) {
    auto shaped = request.Base;
    shaped.Modulation = waveform;
    out << "  \"fsk\": {\n    \"samples_per_symbol\": " << request.Base.SamplesPerSymbol
        << ",\n    \"symbol_rate_baud\": " << request.Base.SymbolRateBaud
        << ",\n    \"tone_spacing_hz\": " << FskToneSpacingHz(shaped)
        << ",\n    \"modulation_index\": " << FskModulationIndex(shaped)
        << ",\n    \"rule\": \"continuous phase, zero initial phase per frame, ascending Gray-labelled tones\"\n  "
           "},\n"
        << "  \"crop\": {\n    \"offset_samples\": 0,\n    \"filter_delay_samples\": 0,\n    \"guard_symbols\": "
           "0\n  },\n";
}

void AppendLinearSidecar(std::ostream &out, const BatchRequest &request, const FrameResult &frame) {
    const auto &base = request.Base;
    out << "  \"linear\": {\n    \"pulse\": \"" << (base.Pulse == Pulse::RRC ? "RRC" : "Rectangular")
        << "\",\n    \"roll_off\": " << base.RollOff << ",\n    \"span_symbols\": " << base.SpanSymbols
        << ",\n    \"samples_per_symbol\": " << base.SamplesPerSymbol
        << ",\n    \"symbol_rate_baud\": " << base.SymbolRateBaud << "\n  },\n"
        << "  \"crop\": {\n    \"offset_samples\": " << frame.CropOffset
        << ",\n    \"filter_delay_samples\": " << frame.FilterDelaySamples
        << ",\n    \"guard_symbols\": " << (base.Pulse == Pulse::RRC ? base.SpanSymbols : 0) << "\n  },\n";
}

void AppendChannelSidecar(std::ostream &out, const BatchRequest &request, const FrameResult &frame) {
    const auto &noise = frame.Noise;
    if (noise.AwgnApplied) {
        out << "  \"awgn\": {\n    \"requested_snr_db\": " << noise.RequestedSnrDb
            << ",\n    \"reference_power\": " << noise.ReferencePower
            << ",\n    \"reference_interval\": {\"begin\": " << noise.ReferenceBegin
            << ", \"end\": " << noise.ReferenceEnd << "}"
            << ",\n    \"added_noise_power\": " << noise.AddedNoisePower << ",\n    \"noise_seed\": " << noise.NoiseSeed
            << ",\n    \"snr_definition\": \"clean frame power / added complex noise power\"\n  },\n";
    }
    if (frame.ImpairmentsApplied) {
        out << "  \"impairments\": " << ImpairmentsJson(request.Base.Impairments, frame.ImpairmentSeed, "  ") << ",\n";
    }
}

/**
 * @brief   Builds the JSON sidecar (version 2, kind "batch_frame") of one frame file.
 * @param[in] request   Batch request.
 * @param[in] location  Axis values and indices of the frame.
 * @param[in] frame     Generated frame.
 * @param[in] data_name File name of the frame samples, relative to the output directory.
 * @return  The JSON document.
 */
std::string FrameSidecar(const BatchRequest &request, const FrameLocation &location, const FrameResult &frame,
                         const std::string &data_name) {
    auto out = JsonStream();
    const auto family = WaveformFamily(location.Waveform);
    out << "{\n  \"version\": 2,\n  \"kind\": \"batch_frame\",\n  \"format\": \"" << DataFormatName(request.Format)
        << "\",\n  \"waveform\": " << JsonQuote(ModulationName(location.Waveform)) << ",\n  \"family\": \""
        << FamilyName(family) << "\",\n  \"frame_size\": " << frame.Samples.size()
        << ",\n  \"sample_rate_hz\": " << frame.SampleRateHz
        << ",\n  \"sample_units\": \"relative amplitude\",\n  \"amplitude_gain\": " << request.Base.AmplitudeGain
        << ",\n  \"point_index\": " << location.PointIndex << ",\n  \"frame_index\": " << location.FrameIndex
        << ",\n  \"data_file\": " << JsonQuote(data_name) << ",\n  \"axes\": {\n    \"seed\": " << location.Seed
        << ",\n    \"snr_db\": ";
    AppendOptionalNumber(out, location.SnrDb);
    out << "\n  },\n  \"derived_seeds\": {\n    \"data\": " << frame.DataSeed << ",\n    \"noise\": " << frame.NoiseSeed
        << "\n  },\n";
    switch (family) {
    case Family::Noise:
        AppendNoiseSourceSidecar(out, request);
        break;
    case Family::Fsk:
        AppendFskSidecar(out, request, location.Waveform);
        AppendChannelSidecar(out, request, frame);
        break;
    default:
        AppendLinearSidecar(out, request, frame);
        AppendChannelSidecar(out, request, frame);
        break;
    }
    out << "  \"timing\": {\n    \"frame_start_s\": 0,\n    \"duration_s\": "
        << (frame.SampleRateHz > 0 ? frame.Samples.size() / frame.SampleRateHz : 0) << "\n  }\n}\n";
    return out.str();
}

/**
 * @brief   Wraps the frame sidecar in a SigMF 1.0.0 metadata document, under `siggen:metadata`.
 * @param[in] frame    Generated frame.
 * @param[in] sidecar  The frame sidecar from FrameSidecar().
 * @return  The `.sigmf-meta` JSON document. Linear frames also carry `siggen:preset` and `siggen:frame` (crop
 *          offset and size), so a reader can regenerate the ideal symbols and measure EVM on the frame.
 */
std::string SigmfFrameMetadata(const FrameResult &frame, const std::string &sidecar) {
    auto out = JsonStream();
    auto detail = sidecar;
    while (!detail.empty() && (detail.back() == '\n' || detail.back() == ' ')) {
        detail.pop_back();
    }
    out << "{\n  \"global\": {\n    \"core:datatype\": \"cf32_le\",\n    \"core:sample_rate\": " << frame.SampleRateHz
        << ",\n    \"core:version\": \"1.0.0\",\n    \"core:description\": \"Synthetic frame from siggen batch\""
        << ",\n    \"core:recorder\": \"siggen\"";
    if (!frame.GeneratorPreset.empty()) {
        out << ",\n    \"siggen:preset\": " << JsonQuote(frame.GeneratorPreset)
            << ",\n    \"siggen:frame\": {\"crop_offset_samples\": " << frame.CropOffset
            << ", \"frame_size\": " << frame.Samples.size() << "}";
    }
    out << ",\n    \"siggen:metadata\": " << detail
        << "\n  },\n  \"captures\": [\n    {\"core:sample_start\": 0, \"core:frequency\": 0}\n  ],\n  "
           "\"annotations\": []\n}\n";
    return out.str();
}

/**
 * @brief   Builds the manifest record (kind "frame") of one completed frame, one JSON object per line.
 * @param[in] request   Batch request.
 * @param[in] location  Axis values and indices of the frame.
 * @param[in] frame     Generated frame.
 * @param[in] data_name File name of the frame samples, relative to the output directory.
 * @return  The record, ending with a newline.
 */
std::string ManifestRecord(const BatchRequest &request, const FrameLocation &location, const FrameResult &frame,
                           const std::string &data_name) {
    auto out = JsonStream();
    out << "{\"kind\":\"frame\",\"point_index\":" << location.PointIndex << ",\"frame_index\":" << location.FrameIndex
        << ",\"path\":" << JsonQuote(data_name) << ",\"sidecar\":" << JsonQuote(MetadataPath(data_name).string())
        << ",\"waveform\":" << JsonQuote(ModulationName(location.Waveform)) << ",\"family\":\""
        << FamilyName(WaveformFamily(location.Waveform)) << "\",\"seed\":" << location.Seed << ",\"snr_db\":";
    AppendOptionalNumber(out, location.SnrDb);
    out << ",\"data_seed\":" << frame.DataSeed << ",\"noise_seed\":" << frame.NoiseSeed
        << ",\"frame_size\":" << frame.Samples.size() << ",\"format\":\"" << FormatName(request.Format) << '"'
        << ",\"crop_offset_samples\":" << frame.CropOffset << ",\"filter_delay_samples\":" << frame.FilterDelaySamples
        << ",\"awgn_applied\":" << (frame.Noise.AwgnApplied ? "true" : "false");
    if (frame.ImpairmentsApplied) {
        const auto &impairments = request.Base.Impairments;
        out << ",\"impairment_seed\":" << frame.ImpairmentSeed << ",\"cfo_hz\":" << impairments.CfoHz
            << ",\"phase_noise_linewidth_hz\":" << impairments.PhaseNoiseLinewidthHz
            << ",\"iq_gain_db\":" << impairments.IqGainDb << ",\"iq_phase_deg\":" << impairments.IqPhaseDeg
            << ",\"dc_offset_i\":" << impairments.DcOffsetI << ",\"dc_offset_q\":" << impairments.DcOffsetQ
            << ",\"adc_bits\":" << impairments.AdcBits;
    }
    if (frame.Noise.AwgnApplied) {
        out << ",\"requested_snr_db\":" << frame.Noise.RequestedSnrDb
            << ",\"reference_power\":" << frame.Noise.ReferencePower
            << ",\"added_noise_power\":" << frame.Noise.AddedNoisePower;
    }
    if (WaveformFamily(location.Waveform) == Family::Noise) {
        out << ",\"noise_power\":" << frame.Noise.AddedNoisePower;
    }
    out << "}\n";
    return out.str();
}

std::string ManifestHeader(const BatchRequest &request, const std::size_t point_count, const std::size_t frame_count) {
    auto out = JsonStream();
    out << "{\"kind\":\"batch_header\",\"manifest_version\":1,\"tool\":\"siggen\",\"format\":\""
        << FormatName(request.Format) << "\",\"frame_size\":" << request.FrameSize
        << ",\"frames_per_point\":" << request.FramesPerPoint << ",\"point_count\":" << point_count
        << ",\"frame_count\":" << frame_count << ",\"waveforms\":";
    AppendJsonArray(out, EffectiveWaveforms(request),
                    [&](const Modulation waveform) { out << JsonQuote(ModulationName(waveform)); });
    out << ",\"seeds\":";
    AppendJsonArray(out, EffectiveSeeds(request), [&](const std::uint32_t seed) { out << seed; });
    out << ",\"snrs_db\":";
    if (request.SnrsDb.empty()) {
        out << "null";
    } else {
        AppendJsonArray(out, request.SnrsDb, [&](const double snr) { out << snr; });
    }
    out << ",\"amplitude_gain\":" << request.Base.AmplitudeGain << ",\"noise_seed\":" << request.Base.NoiseSeed
        << "}\n";
    return out.str();
}

std::string ManifestSummary(const BatchSummary &summary) {
    auto out = JsonStream();
    out << "{\"kind\":\"summary\",\"completed\":true,\"point_count\":" << summary.PointCount
        << ",\"frame_count\":" << summary.FrameCount << "}\n";
    return out.str();
}

// endregion

// region Batch run

std::vector<std::optional<double>> SnrPoints(const Modulation waveform, const BatchRequest &request) {
    std::vector<std::optional<double>> snr_points{std::nullopt};
    if (WaveformFamily(waveform) != Family::Noise && !request.SnrsDb.empty()) {
        snr_points.clear();
        for (const auto snr : request.SnrsDb) {
            snr_points.push_back(snr);
        }
    }
    return snr_points;
}

std::size_t CountPoints(const std::vector<Modulation> &waveforms, const std::size_t seed_count,
                        const BatchRequest &request) {
    std::size_t point_count = 0;
    for (const auto waveform : waveforms) {
        point_count += seed_count * PointsPerSeed(waveform, request);
    }
    return point_count;
}

std::ofstream OpenManifest(const BatchRequest &request, const std::size_t point_count) {
    const auto frame_count = point_count * static_cast<std::size_t>(request.FramesPerPoint);
    std::ofstream manifest(request.OutputDir / "manifest.jsonl", std::ios::binary | std::ios::noreplace);
    manifest.exceptions(std::ios::badbit | std::ios::failbit);
    manifest << ManifestHeader(request, point_count, frame_count);
    manifest.flush();
    return manifest;
}

/**
 * @brief   Generates one frame, writes its sample file and sidecar, then appends its manifest record.
 * @note    The record is appended only after both frame files closed successfully, so the manifest lists only
 *          complete frames.
 */
void WriteFrame(const BatchRequest &request, const GenerationConfig &config, const FrameLocation &location,
                std::ofstream &manifest) {
    const auto frame = GenerateFrame(config, request.FrameSize, location.FrameIndex, location.SnrDb);
    const auto data_name = FrameBasename(location.PointIndex, location.FrameIndex) + FormatExtension(request.Format);
    auto sidecar = FrameSidecar(request, location, frame, data_name);
    if (request.Format == ExportFormat::SigMF) {
        sidecar = SigmfFrameMetadata(frame, sidecar);
    }
    {
        std::ofstream data_file(request.OutputDir / data_name, std::ios::binary | std::ios::noreplace);
        std::ofstream sidecar_file(request.OutputDir / MetadataPath(data_name), std::ios::binary | std::ios::noreplace);
        data_file.exceptions(std::ios::badbit | std::ios::failbit);
        sidecar_file.exceptions(std::ios::badbit | std::ios::failbit);
        WriteSamples(data_file, std::span<const std::complex<float>>(frame.Samples), frame.SampleRateHz,
                     request.Format);
        sidecar_file << sidecar;
        data_file.close();
        sidecar_file.close();
    }
    manifest << ManifestRecord(request, location, frame, data_name);
    manifest.flush();
}

// endregion

} // namespace

/**
 * @brief   Derives the data seed of a batch frame.
 * @param[in] base_seed     Seed of the sweep point.
 * @param[in] waveform      Waveform of the frame.
 * @param[in] frame_index   Index of the frame within its point.
 * @return  A deterministic seed from std::seed_seq over {base seed, waveform id, frame index, 1}.
 * @note    SNR is deliberately excluded so every SNR point of a (waveform, seed) sweep shares the same data and
 *          noise draws.
 */
std::uint32_t DeriveDataSeed(const std::uint32_t base_seed, const Modulation waveform, const std::size_t frame_index) {
    return DeriveSeed({base_seed, static_cast<std::uint32_t>(WaveformId(waveform)),
                       static_cast<std::uint32_t>(frame_index), StreamTagData});
}

/**
 * @brief   Derives the AWGN (or noise source) seed of a batch frame.
 * @param[in] base_seed             Seed of the sweep point.
 * @param[in] waveform              Waveform of the frame.
 * @param[in] frame_index           Index of the frame within its point.
 * @param[in] configured_noise_seed Noise seed of the base configuration.
 * @return  A deterministic seed from std::seed_seq over {base seed, waveform id, frame index, 2, configured seed}.
 * @note    Independent of the SNR, like DeriveDataSeed().
 */
std::uint32_t DeriveNoiseSeed(const std::uint32_t base_seed, const Modulation waveform, const std::size_t frame_index,
                              const std::uint32_t configured_noise_seed) {
    return DeriveSeed({base_seed, static_cast<std::uint32_t>(WaveformId(waveform)),
                       static_cast<std::uint32_t>(frame_index), StreamTagNoise, configured_noise_seed});
}

/**
 * @brief   Derives the channel impairment seed of a batch frame.
 * @param[in] base_seed                     Seed of the sweep point.
 * @param[in] waveform                      Waveform of the frame.
 * @param[in] frame_index                   Index of the frame within its point.
 * @param[in] configured_impairment_seed    Impairment seed of the base configuration.
 * @return  A deterministic seed from std::seed_seq over {base seed, waveform id, frame index, 3, configured seed}.
 * @note    Independent of the SNR, like DeriveDataSeed().
 */
std::uint32_t DeriveImpairmentSeed(const std::uint32_t base_seed, const Modulation waveform,
                                   const std::size_t frame_index, const std::uint32_t configured_impairment_seed) {
    return DeriveSeed({base_seed, static_cast<std::uint32_t>(WaveformId(waveform)),
                       static_cast<std::uint32_t>(frame_index), StreamTagImpairment, configured_impairment_seed});
}

/**
 * @brief   Checks that a frame can be generated, with checked frame arithmetic, without generating samples.
 * @param[in] base          Base configuration.
 * @param[in] waveform      Waveform of the frame, replacing the base modulation.
 * @param[in] frame_size    Samples per frame, 1 to MaxSignalSamples.
 * @note    Throws std::invalid_argument for an unsupported waveform, frame size or configuration, and
 *          std::length_error when the frame with its guard symbols exceeds the symbol limit or the crop does not
 *          fit the generated buffer.
 */
void ValidateFrameRequest(const GenerationConfig &base, const Modulation waveform, const int frame_size) {
    if (!IsValid(waveform)) {
        throw std::invalid_argument("Unsupported modulation");
    }
    if (frame_size < 1 || static_cast<std::size_t>(frame_size) > MaxSignalSamples) {
        throw std::invalid_argument("Frame size must be 1–4194304 samples");
    }
    auto config = base;
    config.Modulation = waveform;
    config.DataSource = DataSource::Random;
    config.Bits.clear();
    if (WaveformFamily(waveform) == Family::Noise) {
        config.NoiseSource.SampleCount = frame_size;
        // WGN ignores the SNR axis entirely; any base AWGN setting is inactive
        config.Awgn.Enabled = false;
        config.Impairments = {};
        Validate(config);
        return;
    }
    // Validate pulse, rate and gain ranges with a trivial length before the frame arithmetic
    config.SymbolCount = 1;
    Validate(config);
    const auto samples_per_symbol = static_cast<std::size_t>(config.SamplesPerSymbol);
    const auto guard = GuardSymbols(config);
    const auto symbols = PayloadSymbols(frame_size, samples_per_symbol) + 2 * guard;
    if (symbols > MaxFrameSymbols) {
        throw std::length_error("Frame generation exceeds symbol limit including guards");
    }
    config.SymbolCount = static_cast<int>(symbols);
    Validate(config);
    const auto taps =
        UsesRrc(config) ? static_cast<std::size_t>(config.SpanSymbols) * samples_per_symbol + 1 : samples_per_symbol;
    const auto full_size = (symbols - 1) * samples_per_symbol + taps + QuadratureDelaySamples(config);
    const auto offset = guard * samples_per_symbol + (UsesRrc(config) ? (taps - 1) / 2 : 0);
    if (offset + static_cast<std::size_t>(frame_size) > full_size) {
        throw std::length_error("Frame crop exceeds generated buffer");
    }
}

/**
 * @brief   Generates one fixed-length frame of the base modulation.
 * @param[in] base          Base configuration; its data source and bits are replaced by seeded random data.
 * @param[in] frame_size    Samples in the frame.
 * @param[in] frame_index   Index of the frame within its point; feeds the seed derivation.
 * @param[in] snr_db        Enables AWGN at this SNR for linear waveforms; empty keeps the base noise setting.
 *                          Noise sources ignore it.
 * @return  The frame. Its timestamps always start at zero; the crop offset is reported separately.
 * @note    Symbol frames are generated with guard symbols and cropped after the filter delay. AWGN measures its
 *          reference power over the retained clean frame; impairments run after it on the cropped frame. Throws
 *          like ValidateFrameRequest().
 */
FrameResult GenerateFrame(const GenerationConfig &base, const int frame_size, const std::size_t frame_index,
                          const std::optional<double> &snr_db) {
    ValidateFrameRequest(base, base.Modulation, frame_size);
    FrameResult frame;
    frame.DataSeed = DeriveDataSeed(base.Seed, base.Modulation, frame_index);
    frame.NoiseSeed = DeriveNoiseSeed(base.Seed, base.Modulation, frame_index, base.NoiseSeed);
    std::optional<double> effective_snr = snr_db;
    if (!effective_snr && base.Awgn.Enabled) {
        effective_snr = base.Awgn.SnrDb;
    }
    auto config = base;
    config.DataSource = DataSource::Random;
    config.Bits.clear();
    config.Awgn.Enabled = false;
    config.Impairments = {};
    config.NoiseSeed = frame.NoiseSeed;
    if (WaveformFamily(base.Modulation) == Family::Noise) {
        return GenerateNoiseFrame(config, frame_size, std::move(frame));
    }
    frame = GenerateSymbolFrame(config, frame_size, std::move(frame));
    if (effective_snr) {
        frame.Noise = AddAwgn(frame.Samples, 0, frame.Samples.size(), *effective_snr, frame.NoiseSeed);
    }
    if (base.Impairments.Active()) {
        frame.ImpairmentSeed = DeriveImpairmentSeed(base.Seed, base.Modulation, frame_index, base.ImpairmentSeed);
        ApplyImpairments(frame.Samples, frame.SampleRateHz, base.Impairments, frame.ImpairmentSeed);
        frame.ImpairmentsApplied = true;
    }
    return frame;
}

/**
 * @brief   Validates a complete sweep, including checked size arithmetic, before any output is created.
 * @param[in] request   Batch request.
 * @note    Throws std::invalid_argument for invalid settings, duplicate or invalid axis values and frames that
 *          cannot be generated, std::runtime_error when the output directory already exists, and std::length_error
 *          when the sweep exceeds MaxBatchFrames frames.
 */
void ValidateBatch(const BatchRequest &request) {
    ValidateBatchSettings(request);
    const auto waveforms = EffectiveWaveforms(request);
    const auto seeds = EffectiveSeeds(request);
    ValidateSweepAxes(waveforms, seeds, request.SnrsDb);
    std::size_t points = 0;
    for (const auto waveform : waveforms) {
        ValidateFrameRequest(request.Base, waveform, request.FrameSize);
        ValidateAwgnSettings(waveform, request);
        const auto per_seed = PointsPerSeed(waveform, request);
        if (seeds.size() > (MaxBatchFrames - points) / per_seed) {
            throw std::length_error("Batch exceeds frame limit");
        }
        points += seeds.size() * per_seed;
    }
    if (request.FramesPerPoint > static_cast<int>(MaxBatchFrames / points)) {
        throw std::length_error("Batch exceeds frame limit");
    }
}

/**
 * @brief   Validates and runs a sweep, writing every frame, its sidecar and the manifest to a new directory.
 * @param[in] request   Batch request; the output directory must not exist (existing directories are left
 *                      untouched).
 * @return  The number of points and frames written.
 * @note    Throws like ValidateBatch() before creating anything. Fails fast on generation or write errors and
 *          keeps the frames already completed; an interrupted run simply lacks the summary record.
 */
BatchSummary RunBatch(const BatchRequest &request) {
    ValidateBatch(request);
    if (!std::filesystem::create_directory(request.OutputDir)) {
        throw std::runtime_error("Batch output directory already exists: " + request.OutputDir.string());
    }
    const auto waveforms = EffectiveWaveforms(request);
    const auto seeds = EffectiveSeeds(request);
    // Total counts are known up front; the header advertises the full sweep
    auto manifest = OpenManifest(request, CountPoints(waveforms, seeds.size(), request));
    BatchSummary summary;
    FrameLocation location;
    for (const auto waveform : waveforms) {
        location.Waveform = waveform;
        const auto snr_points = SnrPoints(waveform, request);
        for (const auto seed : seeds) {
            location.Seed = seed;
            for (const auto &snr : snr_points) {
                location.SnrDb = snr;
                auto config = request.Base;
                config.Modulation = waveform;
                config.Seed = seed;
                for (auto frame_index = 0; frame_index < request.FramesPerPoint; ++frame_index) {
                    location.FrameIndex = static_cast<std::size_t>(frame_index);
                    WriteFrame(request, config, location, manifest);
                    ++summary.FrameCount;
                }
                ++location.PointIndex;
                ++summary.PointCount;
            }
        }
    }
    manifest << ManifestSummary(summary);
    manifest.flush();
    manifest.close();
    return summary;
}

} // namespace Core
