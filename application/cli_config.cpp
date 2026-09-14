#include "cli_config.h"
#include "preset.h"
#include <charconv>
#include <cstdlib>
#include <stdexcept>

namespace {
bool supplied(const CLI::App& source, const char* name) {
    const auto* option = source.get_option(name);
    return option != nullptr && option->count() > 0;
}
double parse_double(const std::string& text, const char* what) {
    double value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument(std::string(what) + " must be a number: " + text);
    return value;
}
void reject(bool condition, const char* message) {
    if (condition) throw std::invalid_argument(message);
}
// Apply defaults -> preset -> explicit options for one parsed App (app or batch).
iq::GenerationConfig resolve_base(const CLI::App& source, const CommandLine& cli, bool for_batch) {
    iq::GenerationConfig config;
    if (supplied(source, "--preset")) config = iq::load_preset(cli.preset);

    if (supplied(source, "--modulation")) {
        iq::Modulation modulation;
        if (!iq::parse_modulation(cli.modulation, modulation))
            throw std::invalid_argument("Unknown --modulation: " + cli.modulation);
        iq::select_waveform(config, modulation);
    }

    bool has_noise = iq::waveform_family(config.modulation) == iq::Family::Noise;
    bool has_linear = !has_noise;
    if (for_batch && !cli.batch_modulations.empty()) {
        has_noise = has_linear = false;
        for (const auto& name : cli.batch_modulations) {
            iq::Modulation waveform;
            if (!iq::parse_modulation(name, waveform))
                throw std::invalid_argument("Unknown --modulations entry: " + name);
            const auto& info = iq::waveform_descriptor(waveform);
            has_noise |= info.family == iq::Family::Noise;
            has_linear |= info.shaped;
        }
    }
    // Only the preset's active family supplies settings. The other batch family
    // starts with defaults; explicit options below can configure both families.
    if (for_batch) {
        const auto source_noise = config.noise_source;
        if (iq::waveform_family(config.modulation) == iq::Family::Noise) {
            iq::select_waveform(config, iq::Modulation::BPSK);
            config.noise_source = source_noise;
            if (!has_linear) config.modulation = iq::Modulation::WGN;
        } else {
            config.noise_source = iq::NoiseSourceSettings{};
        }
    }
    reject(!has_noise && (supplied(source, "--samples") || supplied(source, "--sample-rate") ||
                      supplied(source, "--noise-power")),
           "Noise source options (--samples/--sample-rate/--noise-power) require --modulation WGN");
    reject(!has_linear && (supplied(source, "--symbols") || supplied(source, "--symbol-rate") ||
                     supplied(source, "--sps") || supplied(source, "--pulse") ||
                     supplied(source, "--roll-off") || supplied(source, "--span") ||
                     supplied(source, "--bits") || supplied(source, "--data-source") ||
                     supplied(source, "--snr-db")),
           "Linear options do not apply to --modulation WGN");
    if (for_batch)
        reject(supplied(source, "--symbols") || supplied(source, "--samples") ||
                   supplied(source, "--bits") || supplied(source, "--data-source"),
               "Batch generation rejects --symbols/--samples/--bits/--data-source; frame size fixes length");

    if (supplied(source, "--symbols")) config.symbol_count = cli.symbols;
    if (supplied(source, "--symbol-rate")) config.symbol_rate_baud = cli.symbol_rate;
    if (supplied(source, "--sps")) config.samples_per_symbol = cli.sps;
    if (supplied(source, "--pulse")) {
        iq::Pulse pulse;
        if (!iq::parse_pulse(cli.pulse, pulse)) throw std::invalid_argument("Unknown --pulse: " + cli.pulse);
        config.pulse = pulse;
    }
    if (supplied(source, "--roll-off")) config.roll_off = cli.roll_off;
    if (supplied(source, "--span")) config.span_symbols = cli.span;
    if (supplied(source, "--gain")) config.amplitude_gain = cli.gain;
    if (supplied(source, "--seed")) config.seed = cli.seed;
    if (supplied(source, "--noise-seed")) config.noise_seed = cli.noise_seed;
    if (supplied(source, "--samples")) config.noise_source.sample_count = cli.samples;
    if (supplied(source, "--sample-rate")) config.noise_source.sample_rate_hz = cli.sample_rate;
    if (supplied(source, "--noise-power")) config.noise_source.noise_power = cli.noise_power;

    const bool bits_supplied = supplied(source, "--bits");
    const bool source_random = supplied(source, "--data-source") && cli.data_source == "random";
    if (bits_supplied && source_random)
        throw std::invalid_argument("--bits conflicts with --data-source random");
    if (supplied(source, "--data-source"))
        config.data_source = cli.data_source == "explicit" ? iq::DataSource::Explicit : iq::DataSource::Random;
    if (bits_supplied) {
        config.bits = cli.bits;
        config.data_source = iq::DataSource::Explicit;
    }

    if (supplied(source, "--snr-db")) {
        if (cli.snr_db == "off") config.awgn.enabled = false;
        else {
            config.awgn.enabled = true;
            config.awgn.snr_db = parse_double(cli.snr_db, "--snr-db");
        }
    }
    iq::validate(config);
    return config;
}
}

iq::GenerationConfig resolve_config(const CommandLine& cli) { return resolve_base(cli.app, cli, false); }

iq::ExportFormat resolve_format(const std::string& format) {
    if (format == "cf32") return iq::ExportFormat::BinaryFloat32;
    if (format == "csv") return iq::ExportFormat::CSV;
    throw std::invalid_argument("Unknown --format: " + format);
}

std::string resolve_output(const CommandLine& cli) {
    if (!cli.output.empty()) return cli.output;
    return resolve_format(cli.format) == iq::ExportFormat::CSV ? "signal.csv" : "signal.iq";
}

iq::BatchRequest resolve_batch(const CommandLine& cli) {
    // Root signal/export options belong to single generation. Accepting them here
    // would silently discard values because only subcommand counts are resolved.
    for (const auto* option : cli.app.get_options()) {
        if (option->count() && option->get_name() != "--log-level")
            throw std::invalid_argument("With batch, put signal and format options after 'batch'; root option: " +
                                        option->get_name());
    }
    reject(cli.batch_overwrite, "batch --overwrite is rejected; choose a new --output-dir");
    iq::BatchRequest request;
    request.base = resolve_base(*cli.batch, cli, true);
    for (const auto& name : cli.batch_modulations) {
        iq::Modulation modulation;
        if (!iq::parse_modulation(name, modulation)) throw std::invalid_argument("Unknown --modulations entry: " + name);
        request.waveforms.push_back(modulation);
    }
    request.seeds = cli.batch_seeds;
    request.snrs_db = cli.batch_snrs_db;
    request.frame_size = cli.frame_size;
    request.frames_per_point = cli.frames_per_point;
    request.format = resolve_format(cli.batch_format);
    request.output_dir = cli.output_dir;
    return request;
}
