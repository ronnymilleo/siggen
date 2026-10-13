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
#include <string>

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

template <class T> std::expected<T, std::string> Number(const std::string &text) {
    T result{};
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::unexpected("Malformed preset number: " + text);
    }
    return result;
}

std::expected<bool, std::string> Boolean(const std::string &text) {
    if (text == "true") {
        return true;
    }
    if (text == "false") {
        return false;
    }
    return std::unexpected("Malformed preset boolean: " + text);
}

void StripCarriageReturn(std::string &line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
}

/**
 * @brief   Reads the header line and tells which dialect follows.
 * @return  True for the legacy header, false for the current one; an error for any other header.
 */
std::expected<bool, std::string> ReadHeader(std::istream &input) {
    std::string line;
    std::getline(input, line);
    StripCarriageReturn(line);
    const bool legacy = line == LegacyPresetHeader;
    if (!legacy && line != PresetHeader) {
        return std::unexpected(std::string("Unrecognized preset header"));
    }
    return legacy;
}

std::expected<PresetFields, std::string> ReadFields(std::istream &input) {
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
            return std::unexpected(std::string("Malformed or duplicate preset field"));
        }
    }
    return fields;
}

// Removes the field so the fields left at the end are the unknown ones
std::expected<std::string, std::string> TakeField(PresetFields &fields, const char *key) {
    auto field = fields.find(key);
    if (field == fields.end()) {
        return std::unexpected(std::string("Missing preset field: ") + key);
    }
    auto value = field->second;
    fields.erase(field);
    return value;
}

// Reads a field and parses it as a number in one step
template <class T> std::expected<T, std::string> TakeNumber(PresetFields &fields, const char *key) {
    const auto text = TakeField(fields, key);
    if (!text.has_value()) {
        return std::unexpected(text.error());
    }
    return Number<T>(*text);
}

std::expected<void, std::string> ParseModulationField(PresetFields &fields, const std::string &version,
                                                      GenerationConfig &config) {
    const auto modulation = TakeField(fields, "Modulation");
    if (!modulation.has_value()) {
        return std::unexpected(modulation.error());
    }
    // Version 1 knew only BPSK, QPSK and 16-QAM; FSK arrived with version 4
    if (!ParseModulation(*modulation, config.Modulation) ||
        (version == "1" && config.Modulation != Modulation::BPSK && config.Modulation != Modulation::QPSK &&
         config.Modulation != Modulation::QAM16) ||
        (WaveformFamily(config.Modulation) == Family::Fsk && version != "4")) {
        return std::unexpected(std::string("Unsupported preset modulation"));
    }
    return {};
}

std::expected<Pulse, std::string> ParsePulse(const std::string &text) {
    if (text == "RRC") {
        return Pulse::RRC;
    }
    if (text == "Rectangular") {
        return Pulse::Rectangular;
    }
    return std::unexpected(std::string("Unsupported preset pulse"));
}

std::expected<DataSource, std::string> ParseDataSource(const std::string &text) {
    if (text == "Random") {
        return DataSource::Random;
    }
    if (text == "Explicit") {
        return DataSource::Explicit;
    }
    return std::unexpected(std::string("Unsupported preset data source"));
}

std::expected<void, std::string> ParseNoiseFields(PresetFields &fields, GenerationConfig &config) {
    const auto sample_count = TakeNumber<int>(fields, "NoiseSampleCount");
    if (!sample_count.has_value()) {
        return std::unexpected(sample_count.error());
    }
    config.NoiseSource.SampleCount = *sample_count;
    const auto sample_rate = TakeNumber<double>(fields, "NoiseSampleRateHz");
    if (!sample_rate.has_value()) {
        return std::unexpected(sample_rate.error());
    }
    config.NoiseSource.SampleRateHz = *sample_rate;
    const auto noise_power = TakeNumber<double>(fields, "NoisePower");
    if (!noise_power.has_value()) {
        return std::unexpected(noise_power.error());
    }
    config.NoiseSource.NoisePower = *noise_power;
    const auto awgn_enabled = TakeField(fields, "AwgnEnabled").and_then(Boolean);
    if (!awgn_enabled.has_value()) {
        return std::unexpected(awgn_enabled.error());
    }
    config.Awgn.Enabled = *awgn_enabled;
    const auto snr_db = TakeNumber<double>(fields, "AwgnSnrDb");
    if (!snr_db.has_value()) {
        return std::unexpected(snr_db.error());
    }
    config.Awgn.SnrDb = *snr_db;
    const auto noise_seed = TakeNumber<std::uint32_t>(fields, "NoiseSeed");
    if (!noise_seed.has_value()) {
        return std::unexpected(noise_seed.error());
    }
    config.NoiseSeed = *noise_seed;
    return {};
}

std::expected<void, std::string> ParseImpairmentFields(PresetFields &fields, GenerationConfig &config) {
    auto &impairments = config.Impairments;
    const auto cfo_hz = TakeNumber<double>(fields, "CfoHz");
    if (!cfo_hz.has_value()) {
        return std::unexpected(cfo_hz.error());
    }
    impairments.CfoHz = *cfo_hz;
    const auto linewidth = TakeNumber<double>(fields, "PhaseNoiseLinewidthHz");
    if (!linewidth.has_value()) {
        return std::unexpected(linewidth.error());
    }
    impairments.PhaseNoiseLinewidthHz = *linewidth;
    const auto iq_gain = TakeNumber<double>(fields, "IqGainDb");
    if (!iq_gain.has_value()) {
        return std::unexpected(iq_gain.error());
    }
    impairments.IqGainDb = *iq_gain;
    const auto iq_phase = TakeNumber<double>(fields, "IqPhaseDeg");
    if (!iq_phase.has_value()) {
        return std::unexpected(iq_phase.error());
    }
    impairments.IqPhaseDeg = *iq_phase;
    const auto dc_i = TakeNumber<double>(fields, "DcOffsetI");
    if (!dc_i.has_value()) {
        return std::unexpected(dc_i.error());
    }
    impairments.DcOffsetI = *dc_i;
    const auto dc_q = TakeNumber<double>(fields, "DcOffsetQ");
    if (!dc_q.has_value()) {
        return std::unexpected(dc_q.error());
    }
    impairments.DcOffsetQ = *dc_q;
    const auto adc_bits = TakeNumber<int>(fields, "AdcBits");
    if (!adc_bits.has_value()) {
        return std::unexpected(adc_bits.error());
    }
    impairments.AdcBits = *adc_bits;
    const auto seed = TakeNumber<std::uint32_t>(fields, "ImpairmentSeed");
    if (!seed.has_value()) {
        return std::unexpected(seed.error());
    }
    config.ImpairmentSeed = *seed;
    return {};
}

/**
 * @brief   Reads the fields of a versioned (non-legacy) preset, except those shared with the legacy dialect.
 * @note    Fields are taken in a fixed order, so the first missing or malformed one decides the error message.
 *          Version 1 imports keep the noise-disabled defaults.
 */
std::expected<void, std::string> ParseVersionedFields(PresetFields &fields, GenerationConfig &config) {
    const auto version = TakeField(fields, "Version");
    if (!version.has_value()) {
        return std::unexpected(version.error());
    }
    if (*version != "1" && *version != "2" && *version != "3" && *version != "4") {
        return std::unexpected(std::string("Unsupported preset version"));
    }
    if (const auto parsed = ParseModulationField(fields, *version, config); !parsed.has_value()) {
        return parsed;
    }
    const auto symbol_rate = TakeNumber<double>(fields, "SymbolRateBaud");
    if (!symbol_rate.has_value()) {
        return std::unexpected(symbol_rate.error());
    }
    config.SymbolRateBaud = *symbol_rate;
    const auto pulse = TakeField(fields, "Pulse").and_then([](const std::string &text) { return ParsePulse(text); });
    if (!pulse.has_value()) {
        return std::unexpected(pulse.error());
    }
    config.Pulse = *pulse;
    const auto span = TakeNumber<int>(fields, "SpanSymbols");
    if (!span.has_value()) {
        return std::unexpected(span.error());
    }
    config.SpanSymbols = *span;
    const auto gain = TakeNumber<double>(fields, "AmplitudeGain");
    if (!gain.has_value()) {
        return std::unexpected(gain.error());
    }
    config.AmplitudeGain = *gain;
    const auto data_source = TakeField(fields, "DataSource").and_then(ParseDataSource);
    if (!data_source.has_value()) {
        return std::unexpected(data_source.error());
    }
    config.DataSource = *data_source;
    const auto seed = TakeNumber<std::uint32_t>(fields, "Seed");
    if (!seed.has_value()) {
        return std::unexpected(seed.error());
    }
    config.Seed = *seed;
    const auto bits = TakeField(fields, "Bits");
    if (!bits.has_value()) {
        return std::unexpected(bits.error());
    }
    if (bits->find_first_not_of("01") != std::string::npos) {
        return std::unexpected(std::string("Preset bits must be binary digits"));
    }
    config.Bits = *bits;
    if (*version != "1") {
        if (const auto parsed = ParseNoiseFields(fields, config); !parsed.has_value()) {
            return parsed;
        }
    }
    if (*version == "4") {
        const auto tone_spacing = TakeNumber<double>(fields, "ToneSpacingHz");
        if (!tone_spacing.has_value()) {
            return std::unexpected(tone_spacing.error());
        }
        config.ToneSpacingHz = *tone_spacing;
    }
    if (*version == "3" || *version == "4") {
        return ParseImpairmentFields(fields, config);
    }
    return {};
}

} // namespace

/**
 * @brief   Writes a configuration as preset text, using the oldest version that holds it.
 * @param[in] config    Configuration; must be valid and its bits binary digits.
 * @return  The preset text, numbers written with max_digits10 in the classic locale; an error for an invalid
 *          configuration or non-binary bits.
 */
std::expected<std::string, std::string> SerializePreset(const GenerationConfig &config) {
    if (const auto validated = Validate(config); !validated.has_value()) {
        return std::unexpected(validated.error());
    }
    if (config.Bits.find_first_not_of("01") != std::string::npos) {
        return std::unexpected(std::string("Preset bits must be binary digits"));
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
 * @return  The validated configuration; fields older versions lack keep their defaults. An error above 1 MiB,
 *          for a bad header, a malformed, duplicate, missing or unknown field, an unsupported version or value,
 *          or an invalid configuration.
 */
std::expected<GenerationConfig, std::string> ParsePreset(const std::string_view text) {
    if (text.size() > MaxPresetBytes) {
        return std::unexpected(std::string("Preset exceeds 1 MiB"));
    }
    std::istringstream input{std::string(text)};
    const auto legacy = ReadHeader(input);
    if (!legacy.has_value()) {
        return std::unexpected(legacy.error());
    }
    auto fields = ReadFields(input);
    if (!fields.has_value()) {
        return std::unexpected(fields.error());
    }
    GenerationConfig config;
    if (*legacy) {
        fields->erase("ConstellationStart");
    } else if (const auto parsed = ParseVersionedFields(*fields, config); !parsed.has_value()) {
        return std::unexpected(parsed.error());
    }
    const auto symbols = TakeNumber<int>(*fields, "NumberOfSymbols");
    if (!symbols.has_value()) {
        return std::unexpected(symbols.error());
    }
    config.SymbolCount = *symbols;
    const auto sps = TakeNumber<int>(*fields, "SamplesPerSymbol");
    if (!sps.has_value()) {
        return std::unexpected(sps.error());
    }
    config.SamplesPerSymbol = *sps;
    const auto roll_off = TakeNumber<double>(*fields, "RRCBeta");
    if (!roll_off.has_value()) {
        return std::unexpected(roll_off.error());
    }
    config.RollOff = *roll_off;
    if (!fields->empty()) {
        return std::unexpected("Unknown preset field: " + fields->begin()->first);
    }
    if (const auto validated = Validate(config); !validated.has_value()) {
        return std::unexpected(validated.error());
    }
    return config;
}

/**
 * @brief   Extracts the notes of a preset: the lines that start with '#'.
 * @param[in] text  Preset text.
 * @return  The notes without the marker (and one following space), joined by newlines.
 * @note    ParsePreset() ignores these lines; the guided presets in app-gui/examples/ use them to carry a short lesson.
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
 * @return  Nothing on success; an error like SerializePreset() before touching the file, or on an I/O failure.
 */
std::expected<void, std::string> SavePreset(const std::filesystem::path &path, const GenerationConfig &config) {
    const auto text = SerializePreset(config);
    if (!text.has_value()) {
        return std::unexpected(text.error());
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return std::unexpected("Cannot open preset for writing: " + path.string());
    }
    file << *text;
    file.close();
    if (file.fail()) {
        return std::unexpected("Failed writing preset: " + path.string());
    }
    return {};
}

/**
 * @brief   Reads a preset file into a configuration.
 * @param[in] path  Preset file.
 * @return  The validated configuration; an error when the file cannot be opened or read, above 1 MiB, or like
 *          ParsePreset() for invalid content.
 */
std::expected<GenerationConfig, std::string> LoadPreset(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected("Cannot open preset: " + path.string());
    }
    std::string text;
    char buffer[4096];
    while (file.read(buffer, sizeof buffer) || file.gcount()) {
        text.append(buffer, static_cast<std::size_t>(file.gcount()));
        if (text.size() > MaxPresetBytes) {
            return std::unexpected(std::string("Preset exceeds 1 MiB"));
        }
    }
    if (!file.eof() || file.bad()) {
        return std::unexpected(std::string("Failed reading preset"));
    }
    return ParsePreset(text);
}

/**
 * @brief   Reads the notes of a preset file without parsing its settings.
 * @param[in] path  Preset file.
 * @return  The notes, as PresetNotes() returns them; an error when the file cannot be opened or above 1 MiB.
 */
std::expected<std::string, std::string> LoadPresetNotes(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected("Cannot open preset: " + path.string());
    }
    std::string text(MaxPresetBytes + 1, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(file.gcount()));
    if (text.size() > MaxPresetBytes) {
        return std::unexpected(std::string("Preset exceeds 1 MiB"));
    }
    return PresetNotes(text);
}

} // namespace Core
