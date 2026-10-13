/**
 * @file    recording.cpp
 * @brief   Reads SigMF recordings back and rebuilds the generated signal they came from.
 */

#include "recording.h"

#include "preset.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

namespace Core {

namespace {

using Json = nlohmann::json;

std::expected<std::string, std::string> ReadText(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("Cannot open " + path.string());
    }
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

std::expected<std::vector<char>, std::string> ReadBytes(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("Cannot open " + path.string());
    }
    return std::vector<char>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

// Little-endian words, independent of the host byte order
std::uint32_t Word32(const char *bytes) {
    std::uint32_t word = 0;
    for (int k = 3; k >= 0; --k) {
        word = (word << 8) | static_cast<unsigned char>(bytes[k]);
    }
    return word;
}

std::int16_t Word16(const char *bytes) {
    return static_cast<std::int16_t>(
        static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[0]) | (static_cast<unsigned char>(bytes[1]) << 8)));
}

std::expected<std::vector<std::complex<float>>, std::string> DecodeCf32(const std::vector<char> &bytes) {
    if (bytes.size() % 8) {
        return std::unexpected(std::string("Data size is not a whole number of cf32_le samples"));
    }
    std::vector<std::complex<float>> samples;
    samples.reserve(bytes.size() / 8);
    for (std::size_t k = 0; k < bytes.size(); k += 8) {
        samples.emplace_back(std::bit_cast<float>(Word32(&bytes[k])), std::bit_cast<float>(Word32(&bytes[k + 4])));
    }
    return samples;
}

// Full scale is 32768, so the samples land in [-1, 1)
std::expected<std::vector<std::complex<float>>, std::string> DecodeCi16(const std::vector<char> &bytes) {
    if (bytes.size() % 4) {
        return std::unexpected(std::string("Data size is not a whole number of ci16_le samples"));
    }
    std::vector<std::complex<float>> samples;
    samples.reserve(bytes.size() / 4);
    for (std::size_t k = 0; k < bytes.size(); k += 4) {
        samples.emplace_back(Word16(&bytes[k]) / 32768.0f, Word16(&bytes[k + 2]) / 32768.0f);
    }
    return samples;
}

std::expected<std::vector<std::complex<float>>, std::string> Decode(const std::vector<char> &bytes,
                                                                    const std::string &datatype) {
    std::vector<std::complex<float>> samples;
    if (datatype == "cf32_le") {
        const auto decoded = DecodeCf32(bytes);
        if (!decoded.has_value()) {
            return std::unexpected(decoded.error());
        }
        samples = std::move(*decoded);
    } else if (datatype == "ci16_le") {
        const auto decoded = DecodeCi16(bytes);
        if (!decoded.has_value()) {
            return std::unexpected(decoded.error());
        }
        samples = std::move(*decoded);
    } else {
        return std::unexpected("Unsupported datatype '" + datatype + "' (supported: cf32_le, ci16_le)");
    }
    for (auto sample : samples) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            return std::unexpected(std::string("Recording contains non-finite samples"));
        }
    }
    return samples;
}

// The member `key` of `object`; `where` names the object in the error message
std::expected<const Json *, std::string> Require(const Json &object, const char *key, const char *where) {
    if (!object.is_object() || !object.contains(key)) {
        return std::unexpected(std::string(where) + " is missing '" + key + "'");
    }
    return &object.at(key);
}

// siggen's own fields are optional: a malformed preset or frame span is ignored, not an error
void ReadSiggenFields(const Json &global, Recording &recording) {
    if (global.contains("siggen:preset") && global.at("siggen:preset").is_string()) {
        if (const auto config = ParsePreset(global.at("siggen:preset").get<std::string>()); config.has_value()) {
            recording.Config = std::move(*config);
        }
    }
    if (global.contains("siggen:frame") && global.at("siggen:frame").is_object()) {
        const auto &frame = global.at("siggen:frame");
        if (frame.contains("crop_offset_samples") && frame.contains("frame_size") &&
            frame.at("crop_offset_samples").is_number_unsigned() && frame.at("frame_size").is_number_unsigned()) {
            recording.Frame = Recording::FrameSpan{frame.at("crop_offset_samples").get<std::size_t>(),
                                                   frame.at("frame_size").get<std::size_t>()};
        }
    }
}

std::expected<Recording, std::string> ReadSigmf(std::filesystem::path meta_path) {
    if (meta_path.extension() == ".sigmf-data") {
        meta_path.replace_extension(".sigmf-meta");
    }
    const auto text = ReadText(meta_path);
    if (!text.has_value()) {
        return std::unexpected(text.error());
    }
    // Non-throwing parse: a discarded value means the document is not JSON
    const auto root = Json::parse(*text, nullptr, false);
    if (root.is_discarded()) {
        return std::unexpected("Invalid JSON: " + meta_path.string());
    }
    const auto global = Require(root, "global", "SigMF metadata");
    if (!global.has_value()) {
        return std::unexpected(global.error());
    }
    Recording recording;
    const auto datatype = Require(**global, "core:datatype", "SigMF global object");
    if (!datatype.has_value()) {
        return std::unexpected(datatype.error());
    }
    const auto sample_rate = Require(**global, "core:sample_rate", "SigMF global object");
    if (!sample_rate.has_value()) {
        return std::unexpected(sample_rate.error());
    }
    if (!(*datatype)->is_string() || !(*sample_rate)->is_number() || !((*sample_rate)->get<double>() > 0)) {
        return std::unexpected(
            std::string("SigMF core:datatype must be a string and core:sample_rate a positive number"));
    }
    recording.Datatype = (*datatype)->get<std::string>();
    recording.SampleRateHz = (*sample_rate)->get<double>();
    if ((*global)->contains("core:description") && (*global)->at("core:description").is_string()) {
        recording.Description = (*global)->at("core:description").get<std::string>();
    }
    auto data_path = meta_path;
    data_path.replace_extension(".sigmf-data");
    const auto bytes = ReadBytes(data_path);
    if (!bytes.has_value()) {
        return std::unexpected(bytes.error());
    }
    const auto samples = Decode(*bytes, recording.Datatype);
    if (!samples.has_value()) {
        return std::unexpected(samples.error());
    }
    recording.Samples = std::move(*samples);
    ReadSiggenFields(**global, recording);
    return recording;
}

} // namespace

/**
 * @brief   Reads a SigMF recording given its `.sigmf-meta` or `.sigmf-data` path.
 * @param[in] path  Path of either file of the recording.
 * @return  The samples with the sample rate, datatype, description and, when present, siggen's configuration
 *          and batch frame span; a descriptive error otherwise (unsupported file or datatype, missing fields,
 *          invalid JSON, truncated data, non-finite samples).
 * @note    Supports cf32_le and ci16_le (scaled by 1/32768).
 */
std::expected<Recording, std::string> ReadRecording(const std::filesystem::path &path) {
    if (path.extension() != ".sigmf-meta" && path.extension() != ".sigmf-data") {
        return std::unexpected("Unsupported file '" + path.string() +
                               "': expected a .sigmf-meta or .sigmf-data recording");
    }
    return ReadSigmf(path);
}

/**
 * @brief   Rebuilds a GeneratedSignal around the recording's samples using its embedded configuration.
 * @param[in] recording  Recording read by ReadRecording().
 * @return  The regenerated signal (symbols and bits) carrying the file's samples. Empty when the file has no
 *          configuration, is a batch frame, its length does not match, or the configuration fails to generate.
 */
std::optional<GeneratedSignal> SignalFromRecording(const Recording &recording) {
    if (!recording.Config || recording.Frame) {
        return std::nullopt;
    }
    auto generated = Generate(*recording.Config);
    if (!generated.has_value() || generated->Samples.size() != recording.Samples.size()) {
        return std::nullopt;
    }
    auto signal = std::move(*generated);
    signal.Samples = recording.Samples;
    return signal;
}

/**
 * @brief   Places a batch frame back into its full buffer and finds the symbols it can score.
 * @param[in] recording  Recording read by ReadRecording().
 * @return  The full buffer (the regenerated clean signal outside the frame) and the symbol range whose
 *          matched-filter windows lie wholly inside the frame. Empty unless the file is a linear batch frame that
 *          carries its generator preset, its span fits the buffer, and at least one symbol qualifies.
 */
std::optional<FrameScoring> ScoreFrame(const Recording &recording) {
    if (!recording.Config || !recording.Frame || WaveformFamily(recording.Config->Modulation) != Family::Linear) {
        return std::nullopt;
    }
    const auto [offset, size] = *recording.Frame;
    if (size != recording.Samples.size()) {
        return std::nullopt;
    }
    const auto generated = Generate(*recording.Config);
    if (!generated.has_value()) {
        return std::nullopt;
    }
    FrameScoring scoring{std::move(*generated), 0, 0};
    auto &signal = scoring.Signal;
    if (offset + size > signal.Samples.size()) {
        return std::nullopt;
    }
    std::copy(recording.Samples.begin(), recording.Samples.end(),
              signal.Samples.begin() + static_cast<std::ptrdiff_t>(offset));
    const auto sps = static_cast<std::size_t>(signal.Config.SamplesPerSymbol);
    // The matched filter for symbol k reads samples [k*sps, k*sps + taps - 1 + quadrature delay] (see MatchedSymbols)
    const auto taps = UsesRrc(signal.Config) ? 2 * signal.FilterDelaySamples + 1 : sps;
    const auto reach = taps - 1 + QuadratureDelaySamples(signal.Config);
    scoring.FirstSymbol = (offset + sps - 1) / sps;
    if (offset + size < reach + 1) {
        return std::nullopt;
    }
    scoring.EndSymbol = (offset + size - 1 - reach) / sps + 1;
    if (scoring.EndSymbol <= scoring.FirstSymbol) {
        return std::nullopt;
    }
    return scoring;
}

} // namespace Core
