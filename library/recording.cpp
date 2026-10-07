#include "recording.h"
#include "preset.h"
#include <nlohmann/json.hpp>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
namespace iq {
namespace {
std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open " + path.string());
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}
std::vector<char> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open " + path.string());
    return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
std::uint32_t word32(const char* p) {
    std::uint32_t v = 0;
    for (int k = 3; k >= 0; --k) v = (v << 8) | static_cast<unsigned char>(p[k]);
    return v;
}
std::int16_t word16(const char* p) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(static_cast<unsigned char>(p[0]) | (static_cast<unsigned char>(p[1]) << 8)));
}
std::vector<std::complex<float>> decode(const std::vector<char>& bytes, const std::string& datatype) {
    std::vector<std::complex<float>> samples;
    if (datatype == "cf32_le") {
        if (bytes.size() % 8) throw std::runtime_error("Data size is not a whole number of cf32_le samples");
        samples.reserve(bytes.size() / 8);
        for (std::size_t k = 0; k < bytes.size(); k += 8)
            samples.emplace_back(std::bit_cast<float>(word32(&bytes[k])), std::bit_cast<float>(word32(&bytes[k + 4])));
    } else if (datatype == "ci16_le") {
        if (bytes.size() % 4) throw std::runtime_error("Data size is not a whole number of ci16_le samples");
        samples.reserve(bytes.size() / 4);
        for (std::size_t k = 0; k < bytes.size(); k += 4)
            samples.emplace_back(word16(&bytes[k]) / 32768.0f, word16(&bytes[k + 2]) / 32768.0f);
    } else {
        throw std::runtime_error("Unsupported datatype '" + datatype + "' (supported: cf32_le, ci16_le)");
    }
    for (auto value : samples)
        if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) throw std::runtime_error("Recording contains non-finite samples");
    return samples;
}
using Json = nlohmann::json;
Json parse_json(const std::string& text) {
    try {
        return Json::parse(text);
    } catch (const Json::parse_error& e) {
        throw std::invalid_argument(std::string("Invalid JSON: ") + e.what());
    }
}
const Json& require(const Json& object, const char* key, const char* where) {
    if (!object.is_object() || !object.contains(key)) throw std::runtime_error(std::string(where) + " is missing '" + key + "'");
    return object.at(key);
}
Recording read_sigmf(std::filesystem::path meta_path) {
    if (meta_path.extension() == ".sigmf-data") meta_path.replace_extension(".sigmf-meta");
    const auto root = parse_json(read_text(meta_path));
    const auto& global = require(root, "global", "SigMF metadata");
    Recording recording;
    const auto& datatype = require(global, "core:datatype", "SigMF global object");
    const auto& rate = require(global, "core:sample_rate", "SigMF global object");
    if (!datatype.is_string() || !rate.is_number() || !(rate.get<double>() > 0))
        throw std::runtime_error("SigMF core:datatype must be a string and core:sample_rate a positive number");
    recording.datatype = datatype.get<std::string>();
    recording.sample_rate_hz = rate.get<double>();
    if (global.contains("core:description") && global.at("core:description").is_string())
        recording.description = global.at("core:description").get<std::string>();
    auto data_path = meta_path;
    data_path.replace_extension(".sigmf-data");
    recording.samples = decode(read_bytes(data_path), recording.datatype);
    if (global.contains("siggen:preset") && global.at("siggen:preset").is_string()) {
        try { recording.config = parse_preset(global.at("siggen:preset").get<std::string>()); } catch (const std::exception&) {}
    }
    return recording;
}
Recording read_siggen_binary(const std::filesystem::path& path) {
    const auto sidecar = path.string() + ".json";
    if (!std::filesystem::exists(sidecar))
        throw std::runtime_error("No metadata found: expected " + sidecar + " (or a .sigmf-meta file)");
    const auto root = parse_json(read_text(sidecar));
    const auto& format = require(root, "format", "siggen metadata");
    if (!format.is_string() || format.get<std::string>() != "cf32_le")
        throw std::runtime_error("Only cf32 exports and SigMF recordings can be read; CSV is not supported");
    const auto& rate = require(root, "sample_rate_hz", "siggen metadata");
    if (!rate.is_number() || !(rate.get<double>() > 0)) throw std::runtime_error("siggen metadata has an invalid sample_rate_hz");
    Recording recording;
    recording.datatype = "cf32_le";
    recording.sample_rate_hz = rate.get<double>();
    recording.samples = decode(read_bytes(path), recording.datatype);
    return recording;
}
}
Recording read_recording(const std::filesystem::path& path) {
    if (path.extension() == ".sigmf-meta" || path.extension() == ".sigmf-data") return read_sigmf(path);
    return read_siggen_binary(path);
}
std::optional<GeneratedSignal> signal_from_recording(const Recording& recording) {
    if (!recording.config) return std::nullopt;
    auto signal = generate(*recording.config);
    if (signal.samples.size() != recording.samples.size()) return std::nullopt;
    signal.samples = recording.samples;
    return signal;
}
}
