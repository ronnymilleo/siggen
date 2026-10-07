/**
 * @file    preset.cpp
 * @brief   Reads and writes generation settings as INI-like preset files.
 * @details Version 2 predates channel impairments; version 3 adds them. Presets without active impairments are
 *          written as version 2 so existing files stay unchanged. Version 4 adds the FSK tone spacing (and always
 *          carries the impairment fields); it is written only for the FSK family. Version 1 and the legacy
 *          "[SignalGenerator Preset]" header are read only.
 */

#include "preset.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace Core {

namespace {

constexpr int BasePresetVersion = 2;
constexpr int PresetVersionImpairments = 3;
constexpr int PresetVersionFsk = 4;
constexpr std::size_t MaxPresetBytes = 1024 * 1024;
constexpr std::string_view PresetHeader = "[IQ Generator Preset]";
constexpr std::string_view LegacyPresetHeader = "[SignalGenerator Preset]";

using PresetFields = std::map<std::string, std::string>;

int SelectPresetVersion(const GenerationConfig &config) {
    if (WaveformFamily(config.Modulation) == Family::Fsk) {
        return PresetVersionFsk;
    }
    return config.Impairments.Active() ? PresetVersionImpairments : BasePresetVersion;
}

void AppendImpairmentFields(std::ostream &out, const GenerationConfig &config) {
    const auto &impairments = config.Impairments;
    out << "\nCfoHz=" << impairments.CfoHz << "\nPhaseNoiseLinewidthHz=" << impairments.PhaseNoiseLinewidthHz
        << "\nIqGainDb=" << impairments.IqGainDb << "\nIqPhaseDeg=" << impairments.IqPhaseDeg
        << "\nDcOffsetI=" << impairments.DcOffsetI << "\nDcOffsetQ=" << impairments.DcOffsetQ
        << "\nAdcBits=" << impairments.AdcBits << "\nImpairmentSeed=" << config.ImpairmentSeed;
}

template <class T> T Number(const std::string &text) {
    T result{};
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("Malformed preset number: " + text);
    }
    return result;
}

bool Boolean(const std::string &text) {
    if (text == "true") {
        return true;
    }
    if (text == "false") {
        return false;
    }
    throw std::invalid_argument("Malformed preset boolean: " + text);
}

void StripCarriageReturn(std::string &line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
}

/**
 * @brief   Reads the header line and tells which dialect follows.
 * @return  True for the legacy header, false for the current one.
 * @note    Throws std::invalid_argument for any other header.
 */
bool ReadHeader(std::istream &input) {
    std::string line;
    std::getline(input, line);
    StripCarriageReturn(line);
    const bool legacy = line == LegacyPresetHeader;
    if (!legacy && line != PresetHeader) {
        throw std::invalid_argument("Unrecognized preset header");
    }
    return legacy;
}

PresetFields ReadFields(std::istream &input) {
    PresetFields fields;
    std::string line;
    while (std::getline(input, line)) {
        StripCarriageReturn(line);
        // Blank lines and '#' notes
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto split = line.find('=');
        if (split == std::string::npos || !fields.emplace(line.substr(0, split), line.substr(split + 1)).second) {
            throw std::invalid_argument("Malformed or duplicate preset field");
        }
    }
    return fields;
}

// Removes the field so the fields left at the end are the unknown ones
std::string TakeField(PresetFields &fields, const char *key) {
    auto field = fields.find(key);
    if (field == fields.end()) {
        throw std::invalid_argument(std::string("Missing preset field: ") + key);
    }
    auto value = field->second;
    fields.erase(field);
    return value;
}

void ParseModulationField(PresetFields &fields, const std::string &version, GenerationConfig &config) {
    const auto modulation = TakeField(fields, "Modulation");
    // Version 1 knew only BPSK, QPSK and 16-QAM; FSK arrived with version 4
    if (!ParseModulation(modulation, config.Modulation) ||
        (version == "1" && config.Modulation != Modulation::BPSK && config.Modulation != Modulation::QPSK &&
         config.Modulation != Modulation::QAM16) ||
        (WaveformFamily(config.Modulation) == Family::Fsk && version != "4")) {
        throw std::invalid_argument("Unsupported preset modulation");
    }
}

Pulse ParsePulse(const std::string &text) {
    if (text == "RRC") {
        return Pulse::RRC;
    }
    if (text == "Rectangular") {
        return Pulse::Rectangular;
    }
    throw std::invalid_argument("Unsupported preset pulse");
}

DataSource ParseDataSource(const std::string &text) {
    if (text == "Random") {
        return DataSource::Random;
    }
    if (text == "Explicit") {
        return DataSource::Explicit;
    }
    throw std::invalid_argument("Unsupported preset data source");
}

void ParseNoiseFields(PresetFields &fields, GenerationConfig &config) {
    config.NoiseSource.SampleCount = Number<int>(TakeField(fields, "NoiseSampleCount"));
    config.NoiseSource.SampleRateHz = Number<double>(TakeField(fields, "NoiseSampleRateHz"));
    config.NoiseSource.NoisePower = Number<double>(TakeField(fields, "NoisePower"));
    config.Awgn.Enabled = Boolean(TakeField(fields, "AwgnEnabled"));
    config.Awgn.SnrDb = Number<double>(TakeField(fields, "AwgnSnrDb"));
    config.NoiseSeed = Number<std::uint32_t>(TakeField(fields, "NoiseSeed"));
}

void ParseImpairmentFields(PresetFields &fields, GenerationConfig &config) {
    auto &impairments = config.Impairments;
    impairments.CfoHz = Number<double>(TakeField(fields, "CfoHz"));
    impairments.PhaseNoiseLinewidthHz = Number<double>(TakeField(fields, "PhaseNoiseLinewidthHz"));
    impairments.IqGainDb = Number<double>(TakeField(fields, "IqGainDb"));
    impairments.IqPhaseDeg = Number<double>(TakeField(fields, "IqPhaseDeg"));
    impairments.DcOffsetI = Number<double>(TakeField(fields, "DcOffsetI"));
    impairments.DcOffsetQ = Number<double>(TakeField(fields, "DcOffsetQ"));
    impairments.AdcBits = Number<int>(TakeField(fields, "AdcBits"));
    config.ImpairmentSeed = Number<std::uint32_t>(TakeField(fields, "ImpairmentSeed"));
}

/**
 * @brief   Reads the fields of a versioned (non-legacy) preset, except those shared with the legacy dialect.
 * @note    Fields are taken in a fixed order, so the first missing or malformed one decides the error message.
 *          Version 1 imports keep the noise-disabled defaults.
 */
void ParseVersionedFields(PresetFields &fields, GenerationConfig &config) {
    const auto version = TakeField(fields, "Version");
    if (version != "1" && version != "2" && version != "3" && version != "4") {
        throw std::invalid_argument("Unsupported preset version");
    }
    ParseModulationField(fields, version, config);
    config.SymbolRateBaud = Number<double>(TakeField(fields, "SymbolRateBaud"));
    config.Pulse = ParsePulse(TakeField(fields, "Pulse"));
    config.SpanSymbols = Number<int>(TakeField(fields, "SpanSymbols"));
    config.AmplitudeGain = Number<double>(TakeField(fields, "AmplitudeGain"));
    config.DataSource = ParseDataSource(TakeField(fields, "DataSource"));
    config.Seed = Number<std::uint32_t>(TakeField(fields, "Seed"));
    config.Bits = TakeField(fields, "Bits");
    if (config.Bits.find_first_not_of("01") != std::string::npos) {
        throw std::invalid_argument("Preset bits must be binary digits");
    }
    if (version != "1") {
        ParseNoiseFields(fields, config);
    }
    if (version == "4") {
        config.ToneSpacingHz = Number<double>(TakeField(fields, "ToneSpacingHz"));
    }
    if (version == "3" || version == "4") {
        ParseImpairmentFields(fields, config);
    }
}

} // namespace

/**
 * @brief   Writes a configuration as preset text, using the oldest version that holds it.
 * @param[in] config    Configuration; must be valid and its bits binary digits.
 * @return  The preset text, numbers written with max_digits10 in the classic locale.
 * @note    Throws std::invalid_argument for an invalid configuration or non-binary bits.
 */
std::string SerializePreset(const GenerationConfig &config) {
    Validate(config);
    if (config.Bits.find_first_not_of("01") != std::string::npos) {
        throw std::invalid_argument("Preset bits must be binary digits");
    }
    const int version = SelectPresetVersion(config);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << PresetHeader << "\nVersion=" << version
        << "\nModulation=" << ModulationName(config.Modulation) << "\nNumberOfSymbols=" << config.SymbolCount
        << "\nSymbolRateBaud=" << config.SymbolRateBaud << "\nSamplesPerSymbol=" << config.SamplesPerSymbol
        << "\nPulse=" << (config.Pulse == Pulse::RRC ? "RRC" : "Rectangular") << "\nRRCBeta=" << config.RollOff
        << "\nSpanSymbols=" << config.SpanSymbols << "\nAmplitudeGain=" << config.AmplitudeGain
        << "\nDataSource=" << (config.DataSource == DataSource::Random ? "Random" : "Explicit")
        << "\nSeed=" << config.Seed << "\nBits=" << config.Bits
        << "\nNoiseSampleCount=" << config.NoiseSource.SampleCount
        << "\nNoiseSampleRateHz=" << config.NoiseSource.SampleRateHz << "\nNoisePower=" << config.NoiseSource.NoisePower
        << "\nAwgnEnabled=" << (config.Awgn.Enabled ? "true" : "false") << "\nAwgnSnrDb=" << config.Awgn.SnrDb
        << "\nNoiseSeed=" << config.NoiseSeed;
    if (version == PresetVersionFsk) {
        out << "\nToneSpacingHz=" << config.ToneSpacingHz;
    }
    if (version >= PresetVersionImpairments) {
        AppendImpairmentFields(out, config);
    }
    out << '\n';
    return out.str();
}

/**
 * @brief   Reads preset text (any supported version, or the legacy header) into a configuration.
 * @param[in] text  Preset text; CRLF line endings, blank lines and '#' notes are accepted.
 * @return  The validated configuration; fields older versions lack keep their defaults.
 * @note    Throws std::length_error above 1 MiB, and std::invalid_argument for a bad header, a malformed,
 *          duplicate, missing or unknown field, an unsupported version or value, or an invalid configuration.
 */
GenerationConfig ParsePreset(const std::string_view text) {
    if (text.size() > MaxPresetBytes) {
        throw std::length_error("Preset exceeds 1 MiB");
    }
    std::istringstream input{std::string(text)};
    const bool legacy = ReadHeader(input);
    auto fields = ReadFields(input);
    GenerationConfig config;
    if (legacy) {
        fields.erase("ConstellationStart");
    } else {
        ParseVersionedFields(fields, config);
    }
    config.SymbolCount = Number<int>(TakeField(fields, "NumberOfSymbols"));
    config.SamplesPerSymbol = Number<int>(TakeField(fields, "SamplesPerSymbol"));
    config.RollOff = Number<double>(TakeField(fields, "RRCBeta"));
    if (!fields.empty()) {
        throw std::invalid_argument("Unknown preset field: " + fields.begin()->first);
    }
    Validate(config);
    return config;
}

/**
 * @brief   Extracts the notes of a preset: the lines that start with '#'.
 * @param[in] text  Preset text.
 * @return  The notes without the marker (and one following space), joined by newlines.
 * @note    ParsePreset() ignores these lines; the guided presets in presets/ use them to carry a short lesson.
 */
std::string PresetNotes(const std::string_view text) {
    std::istringstream input{std::string(text)};
    std::string line, notes;
    while (std::getline(input, line)) {
        StripCarriageReturn(line);
        if (line.empty() || line.front() != '#') {
            continue;
        }
        auto body = line.substr(1);
        if (!body.empty() && body.front() == ' ') {
            body.erase(0, 1);
        }
        if (!notes.empty()) {
            notes += '\n';
        }
        notes += body;
    }
    return notes;
}

/**
 * @brief   Writes a configuration to a preset file, replacing it if it exists.
 * @param[in] path      Destination file.
 * @param[in] config    Configuration to save.
 * @note    Throws like SerializePreset() before touching the file, and std::ios_base::failure on I/O errors.
 */
void SavePreset(const std::filesystem::path &path, const GenerationConfig &config) {
    const auto text = SerializePreset(config);
    std::ofstream file;
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file.open(path, std::ios::binary | std::ios::trunc);
    file << text;
    file.close();
}

/**
 * @brief   Reads a preset file into a configuration.
 * @param[in] path  Preset file.
 * @return  The validated configuration.
 * @note    Throws std::runtime_error when the file cannot be opened or read, std::length_error above 1 MiB, and
 *          like ParsePreset() for invalid content.
 */
GenerationConfig LoadPreset(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open preset: " + path.string());
    }
    std::string text;
    char buffer[4096];
    while (file.read(buffer, sizeof buffer) || file.gcount()) {
        text.append(buffer, static_cast<std::size_t>(file.gcount()));
        if (text.size() > MaxPresetBytes) {
            throw std::length_error("Preset exceeds 1 MiB");
        }
    }
    if (!file.eof() || file.bad()) {
        throw std::runtime_error("Failed reading preset");
    }
    return ParsePreset(text);
}

/**
 * @brief   Reads the notes of a preset file without parsing its settings.
 * @param[in] path  Preset file.
 * @return  The notes, as PresetNotes() returns them.
 * @note    Throws std::runtime_error when the file cannot be opened and std::length_error above 1 MiB.
 */
std::string LoadPresetNotes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open preset: " + path.string());
    }
    std::string text(MaxPresetBytes + 1, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    if (text.size() > MaxPresetBytes) {
        throw std::length_error("Preset exceeds 1 MiB");
    }
    return PresetNotes(text);
}

} // namespace Core
