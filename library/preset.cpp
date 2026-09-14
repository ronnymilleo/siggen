#include "preset.h"
#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace iq {
namespace {
constexpr int PRESET_VERSION = 2;
template<class T> T number(const std::string& text) {
    T result{};
    auto [end, error] = std::from_chars(text.data(), text.data()+text.size(), result);
    if (error != std::errc{} || end != text.data()+text.size()) throw std::invalid_argument("Malformed preset number: " + text);
    return result;
}
bool boolean(const std::string& text) {
    if (text == "true") return true;
    if (text == "false") return false;
    throw std::invalid_argument("Malformed preset boolean: " + text);
}
}
std::string serialize_preset(const GenerationConfig& c) {
    validate(c);
    if (c.bits.find_first_not_of("01") != std::string::npos) throw std::invalid_argument("Preset bits must be binary digits");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << "[IQ Generator Preset]\nVersion=" << PRESET_VERSION << "\nModulation=" << modulation_name(c.modulation)
        << "\nNumberOfSymbols=" << c.symbol_count << "\nSymbolRateBaud=" << c.symbol_rate_baud
        << "\nSamplesPerSymbol=" << c.samples_per_symbol << "\nPulse=" << (c.pulse == Pulse::RRC ? "RRC" : "Rectangular")
        << "\nRRCBeta=" << c.roll_off << "\nSpanSymbols=" << c.span_symbols << "\nAmplitudeGain=" << c.amplitude_gain
        << "\nDataSource=" << (c.data_source == DataSource::Random ? "Random" : "Explicit")
        << "\nSeed=" << c.seed << "\nBits=" << c.bits
        << "\nNoiseSampleCount=" << c.noise_source.sample_count
        << "\nNoiseSampleRateHz=" << c.noise_source.sample_rate_hz
        << "\nNoisePower=" << c.noise_source.noise_power
        << "\nAwgnEnabled=" << (c.awgn.enabled ? "true" : "false")
        << "\nAwgnSnrDb=" << c.awgn.snr_db
        << "\nNoiseSeed=" << c.noise_seed << '\n';
    return out.str();
}
GenerationConfig parse_preset(std::string_view text) {
    if (text.size() > 1024 * 1024) throw std::length_error("Preset exceeds 1 MiB");
    std::istringstream input{std::string(text)};
    std::string line;
    std::getline(input, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const bool legacy = line == "[SignalGenerator Preset]";
    if (!legacy && line != "[IQ Generator Preset]") throw std::invalid_argument("Unrecognized preset header");
    std::map<std::string, std::string> fields;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto split = line.find('=');
        if (split == std::string::npos || !fields.emplace(line.substr(0,split), line.substr(split+1)).second)
            throw std::invalid_argument("Malformed or duplicate preset field");
    }
    const auto take = [&](const char* key) {
        auto it = fields.find(key);
        if (it == fields.end()) throw std::invalid_argument(std::string("Missing preset field: ") + key);
        auto value = it->second; fields.erase(it); return value;
    };
    GenerationConfig c;
    if (!legacy) {
        const auto version = take("Version");
        if (version != "1" && version != "2") throw std::invalid_argument("Unsupported preset version");
        const auto current = version == "2";
        const auto modulation = take("Modulation");
        if (!parse_modulation(modulation, c.modulation) ||
            (!current && (c.modulation == Modulation::PSK8 || c.modulation == Modulation::QAM64 || c.modulation == Modulation::WGN)))
            throw std::invalid_argument("Unsupported preset modulation");
        c.symbol_rate_baud = number<double>(take("SymbolRateBaud"));
        const auto pulse = take("Pulse");
        if (pulse == "RRC") c.pulse = Pulse::RRC;
        else if (pulse == "Rectangular") c.pulse = Pulse::Rectangular;
        else throw std::invalid_argument("Unsupported preset pulse");
        c.span_symbols = number<int>(take("SpanSymbols"));
        c.amplitude_gain = number<double>(take("AmplitudeGain"));
        const auto source = take("DataSource");
        if (source == "Random") c.data_source = DataSource::Random;
        else if (source == "Explicit") c.data_source = DataSource::Explicit;
        else throw std::invalid_argument("Unsupported preset data source");
        c.seed = number<std::uint32_t>(take("Seed"));
        c.bits = take("Bits");
        if (c.bits.find_first_not_of("01") != std::string::npos) throw std::invalid_argument("Preset bits must be binary digits");
        if (current) {
            c.noise_source.sample_count = number<int>(take("NoiseSampleCount"));
            c.noise_source.sample_rate_hz = number<double>(take("NoiseSampleRateHz"));
            c.noise_source.noise_power = number<double>(take("NoisePower"));
            c.awgn.enabled = boolean(take("AwgnEnabled"));
            c.awgn.snr_db = number<double>(take("AwgnSnrDb"));
            c.noise_seed = number<std::uint32_t>(take("NoiseSeed"));
        }
        // Version 1 imports keep noise-disabled defaults.
    } else fields.erase("ConstellationStart");
    c.symbol_count = number<int>(take("NumberOfSymbols"));
    c.samples_per_symbol = number<int>(take("SamplesPerSymbol"));
    c.roll_off = number<double>(take("RRCBeta"));
    if (!fields.empty()) throw std::invalid_argument("Unknown preset field: " + fields.begin()->first);
    validate(c);
    return c;
}
void save_preset(const std::filesystem::path& path, const GenerationConfig& config) {
    const auto text = serialize_preset(config);
    std::ofstream file;
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file.open(path, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();
}
GenerationConfig load_preset(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open preset: " + path.string());
    std::string text;
    char buffer[4096];
    while (file.read(buffer, sizeof buffer) || file.gcount()) {
        text.append(buffer, static_cast<std::size_t>(file.gcount()));
        if (text.size() > 1024 * 1024) throw std::length_error("Preset exceeds 1 MiB");
    }
    if (!file.eof() || file.bad()) throw std::runtime_error("Failed reading preset");
    return parse_preset(text);
}
}
