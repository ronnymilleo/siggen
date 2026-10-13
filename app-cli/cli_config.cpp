/**
 * @file    cli_config.cpp
 * @brief   Turns parsed command-line options into a validated generation config or batch request.
 */

#include "cli_config.h"

#include "preset.h"
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>

namespace Console {

namespace {

/**
 * @brief   Tells whether an option was given on the command line of one parsed context.
 * @param[in] source  The root App or the batch subcommand.
 * @param[in] name    Option name, such as "--sps".
 * @return  True when the option was given at least once.
 */
bool Supplied(const CLI::App &source, const char *name) {
    const auto *option = source.get_option(name);
    return option != nullptr && option->count() > 0;
}

/**
 * @brief   Parses the whole text as a number.
 * @param[in] text  The option value.
 * @param[in] what  Option name used in the error message.
 * @return  The number, or an error when the text is not entirely a number.
 */
std::expected<double, std::string> ParseDouble(const std::string &text, const char *what) {
    double value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::unexpected(std::string(what) + " must be a number: " + text);
    }
    return value;
}

/**
 * @brief   Turns a rejected condition into an error.
 * @param[in] condition  True when the option combination is not allowed.
 * @param[in] message    User-facing explanation.
 * @return  Nothing when the condition is false; the message as an error otherwise.
 */
std::expected<void, std::string> Reject(bool condition, const char *message) {
    if (condition) {
        return std::unexpected(std::string(message));
    }
    return {};
}

/**
 * @struct  RequestedFamilies
 * @brief   Which groups of options apply to the waveforms a request generates.
 */
struct RequestedFamilies {
    bool Noise = false;
    bool Linear = false;
    bool Tones = false; // 2-FSK/4-FSK only; MSK fixes the spacing
    bool Pulse = false; // Pulse-shaped waveforms only
};

/**
 * @brief   Starts from the defaults, then the preset, then the waveform chosen with --modulation.
 * @param[in] source  The parsed context (root App or batch subcommand).
 * @param[in] cli     Parsed option values.
 * @return  The base configuration before the other explicit options; an error for an unreadable preset or an
 *          unknown --modulation.
 */
std::expected<Core::GenerationConfig, std::string> LoadPresetAndModulation(const CLI::App &source,
                                                                           const CommandLine &cli) {
    Core::GenerationConfig config;
    if (Supplied(source, "--preset")) {
        const auto loaded = Core::LoadPreset(cli.Preset);
        if (!loaded.has_value()) {
            return std::unexpected(loaded.error());
        }
        config = std::move(*loaded);
    }

    if (Supplied(source, "--modulation")) {
        Core::Modulation modulation;
        if (!Core::ParseModulation(cli.Modulation, modulation)) {
            return std::unexpected("Unknown --modulation: " + cli.Modulation);
        }
        Core::SelectWaveform(config, modulation);
    }
    return config;
}

/**
 * @brief   Works out which option groups apply: those of the resolved waveform, or, for a batch with
 *          --modulations, those of any swept waveform.
 * @param[in] config     Configuration after the preset and --modulation.
 * @param[in] cli        Parsed option values.
 * @param[in] for_batch  True when resolving the batch subcommand.
 * @return  The option groups that apply; an error on an unknown --modulations entry.
 */
std::expected<RequestedFamilies, std::string> FamiliesCovered(const Core::GenerationConfig &config,
                                                              const CommandLine &cli, bool for_batch) {
    RequestedFamilies families;
    families.Noise = Core::WaveformFamily(config.Modulation) == Core::Family::Noise;
    families.Linear = !families.Noise;
    families.Tones = config.Modulation == Core::Modulation::FSK2 || config.Modulation == Core::Modulation::FSK4;
    const auto *info = Core::FindWaveform(config.Modulation);
    families.Pulse = info != nullptr && info->Shaped;
    if (for_batch && !cli.BatchModulations.empty()) {
        families = {};
        for (const auto &name : cli.BatchModulations) {
            Core::Modulation waveform;
            if (!Core::ParseModulation(name, waveform)) {
                return std::unexpected("Unknown --modulations entry: " + name);
            }
            const auto *swept = Core::FindWaveform(waveform);
            families.Noise |= swept->Family == Core::Family::Noise;
            families.Linear |= swept->Family != Core::Family::Noise;
            families.Tones |= swept->Family == Core::Family::Fsk && waveform != Core::Modulation::MSK;
            families.Pulse |= swept->Shaped;
        }
    }
    return families;
}

/**
 * @brief   Resets the batch family the preset does not configure to its defaults.
 * @param[in,out] config  Configuration after the preset and --modulation.
 * @param[in] has_linear  Whether the batch generates any symbol-based waveform.
 * @note    Only the preset's active family supplies settings. The other family starts with defaults; explicit
 *          options applied afterwards can configure both families.
 */
void ResetInactiveBatchFamily(Core::GenerationConfig &config, bool has_linear) {
    const auto source_noise = config.NoiseSource;
    if (Core::WaveformFamily(config.Modulation) == Core::Family::Noise) {
        Core::SelectWaveform(config, Core::Modulation::BPSK);
        config.NoiseSource = source_noise;
        if (!has_linear) {
            config.Modulation = Core::Modulation::WGN;
        }
    } else {
        config.NoiseSource = Core::NoiseSourceSettings{};
    }
}

/**
 * @brief   Rejects options that do not apply to the requested waveforms, and the ones a batch fixes itself.
 * @param[in] source     The parsed context (root App or batch subcommand).
 * @param[in] families   The option groups that apply.
 * @param[in] for_batch  True when resolving the batch subcommand.
 * @return  Nothing on success; the first rule broken, in the order they are checked.
 */
std::expected<void, std::string> RejectInapplicableOptions(const CLI::App &source, const RequestedFamilies &families,
                                                           bool for_batch) {
    if (const auto rejected =
            Reject(!families.Noise && (Supplied(source, "--samples") || Supplied(source, "--sample-rate") ||
                                       Supplied(source, "--noise-power")),
                   "Noise source options (--samples/--sample-rate/--noise-power) require "
                   "--modulation WGN");
        !rejected.has_value()) {
        return rejected;
    }
    if (const auto rejected = Reject(
            !families.Linear &&
                (Supplied(source, "--symbols") || Supplied(source, "--symbol-rate") || Supplied(source, "--sps") ||
                 Supplied(source, "--pulse") || Supplied(source, "--roll-off") || Supplied(source, "--span") ||
                 Supplied(source, "--bits") || Supplied(source, "--data-source") || Supplied(source, "--snr-db") ||
                 Supplied(source, "--cfo-hz") || Supplied(source, "--phase-noise-hz") ||
                 Supplied(source, "--iq-gain-db") || Supplied(source, "--iq-phase-deg") || Supplied(source, "--dc-i") ||
                 Supplied(source, "--dc-q") || Supplied(source, "--adc-bits")),
            "Linear options do not apply to --modulation WGN");
        !rejected.has_value()) {
        return rejected;
    }
    if (const auto rejected =
            Reject(!families.Pulse &&
                       (Supplied(source, "--pulse") || Supplied(source, "--roll-off") || Supplied(source, "--span")),
                   "Pulse options (--pulse/--roll-off/--span) do not apply to FSK and MSK, which are not pulse shaped");
        !rejected.has_value()) {
        return rejected;
    }
    if (const auto rejected = Reject(!families.Tones && Supplied(source, "--tone-spacing-hz"),
                                     "--tone-spacing-hz requires --modulation 2-FSK or 4-FSK (MSK fixes the spacing "
                                     "at symbol rate / 2)");
        !rejected.has_value()) {
        return rejected;
    }
    if (for_batch) {
        return Reject(Supplied(source, "--symbols") || Supplied(source, "--samples") || Supplied(source, "--bits") ||
                          Supplied(source, "--data-source"),
                      "Batch generation rejects --symbols/--samples/--bits/--data-source; frame size fixes length");
    }
    return {};
}

/**
 * @brief   Applies the explicit timing, pulse, tone, gain, seed and noise-source options.
 * @param[in] source      The parsed context (root App or batch subcommand).
 * @param[in] cli         Parsed option values.
 * @param[in,out] config  Configuration to update.
 * @return  Nothing on success; an error on an unknown --pulse.
 */
std::expected<void, std::string> ApplySignalOptions(const CLI::App &source, const CommandLine &cli,
                                                    Core::GenerationConfig &config) {
    if (Supplied(source, "--symbols")) {
        config.SymbolCount = cli.Symbols;
    }
    if (Supplied(source, "--symbol-rate")) {
        config.SymbolRateBaud = cli.SymbolRate;
    }
    if (Supplied(source, "--sps")) {
        config.SamplesPerSymbol = cli.Sps;
    }
    if (Supplied(source, "--pulse")) {
        Core::Pulse pulse;
        if (!Core::ParsePulse(cli.Pulse, pulse)) {
            return std::unexpected("Unknown --pulse: " + cli.Pulse);
        }
        config.Pulse = pulse;
    }
    if (Supplied(source, "--tone-spacing-hz")) {
        config.ToneSpacingHz = cli.ToneSpacingHz;
    }
    if (Supplied(source, "--roll-off")) {
        config.RollOff = cli.RollOff;
    }
    if (Supplied(source, "--span")) {
        config.SpanSymbols = cli.Span;
    }
    if (Supplied(source, "--gain")) {
        config.AmplitudeGain = cli.Gain;
    }
    if (Supplied(source, "--seed")) {
        config.Seed = cli.Seed;
    }
    if (Supplied(source, "--noise-seed")) {
        config.NoiseSeed = cli.NoiseSeed;
    }
    if (Supplied(source, "--samples")) {
        config.NoiseSource.SampleCount = cli.Samples;
    }
    if (Supplied(source, "--sample-rate")) {
        config.NoiseSource.SampleRateHz = cli.SampleRate;
    }
    if (Supplied(source, "--noise-power")) {
        config.NoiseSource.NoisePower = cli.NoisePower;
    }
    return {};
}

/**
 * @brief   Applies --data-source and --bits; --bits selects explicit input.
 * @param[in] source      The parsed context (root App or batch subcommand).
 * @param[in] cli         Parsed option values.
 * @param[in,out] config  Configuration to update.
 * @return  Nothing on success; an error when --bits comes with --data-source random.
 */
std::expected<void, std::string> ApplyDataSource(const CLI::App &source, const CommandLine &cli,
                                                 Core::GenerationConfig &config) {
    const bool bits_supplied = Supplied(source, "--bits");
    const bool source_random = Supplied(source, "--data-source") && cli.DataSource == "random";
    if (const auto rejected = Reject(bits_supplied && source_random, "--bits conflicts with --data-source random");
        !rejected.has_value()) {
        return rejected;
    }
    if (Supplied(source, "--data-source")) {
        config.DataSource = cli.DataSource == "explicit" ? Core::DataSource::Explicit : Core::DataSource::Random;
    }
    if (bits_supplied) {
        config.Bits = cli.Bits;
        config.DataSource = Core::DataSource::Explicit;
    }
    return {};
}

/**
 * @brief   Applies --snr-db: a number enables AWGN at that SNR, "off" disables it.
 * @param[in] source      The parsed context (root App or batch subcommand).
 * @param[in] cli         Parsed option values.
 * @param[in,out] config  Configuration to update.
 * @return  Nothing on success; an error when the value is neither "off" nor a number.
 */
std::expected<void, std::string> ApplyAwgn(const CLI::App &source, const CommandLine &cli,
                                           Core::GenerationConfig &config) {
    if (Supplied(source, "--snr-db")) {
        if (cli.SnrDb == "off") {
            config.Awgn.Enabled = false;
        } else {
            const auto snr = ParseDouble(cli.SnrDb, "--snr-db");
            if (!snr.has_value()) {
                return std::unexpected(snr.error());
            }
            config.Awgn.Enabled = true;
            config.Awgn.SnrDb = *snr;
        }
    }
    return {};
}

void ApplyImpairments(const CLI::App &source, const CommandLine &cli, Core::GenerationConfig &config) {
    if (Supplied(source, "--cfo-hz")) {
        config.Impairments.CfoHz = cli.CfoHz;
    }
    if (Supplied(source, "--phase-noise-hz")) {
        config.Impairments.PhaseNoiseLinewidthHz = cli.PhaseNoiseHz;
    }
    if (Supplied(source, "--iq-gain-db")) {
        config.Impairments.IqGainDb = cli.IqGainDb;
    }
    if (Supplied(source, "--iq-phase-deg")) {
        config.Impairments.IqPhaseDeg = cli.IqPhaseDeg;
    }
    if (Supplied(source, "--dc-i")) {
        config.Impairments.DcOffsetI = cli.DcI;
    }
    if (Supplied(source, "--dc-q")) {
        config.Impairments.DcOffsetQ = cli.DcQ;
    }
    if (Supplied(source, "--adc-bits")) {
        config.Impairments.AdcBits = cli.AdcBits;
    }
    if (Supplied(source, "--impairment-seed")) {
        config.ImpairmentSeed = cli.ImpairmentSeed;
    }
}

/**
 * @brief   Applies defaults -> preset -> explicit options for one parsed context, then validates.
 * @param[in] source     The root App for single generation, or the batch subcommand.
 * @param[in] cli        Parsed option values.
 * @param[in] for_batch  True when resolving the batch subcommand.
 * @return  The validated configuration, or the first resolution or validation error.
 */
std::expected<Core::GenerationConfig, std::string> ResolveBase(const CLI::App &source, const CommandLine &cli,
                                                               bool for_batch) {
    auto base = LoadPresetAndModulation(source, cli);
    if (!base.has_value()) {
        return std::unexpected(base.error());
    }
    Core::GenerationConfig config = std::move(*base);
    const auto families = FamiliesCovered(config, cli, for_batch);
    if (!families.has_value()) {
        return std::unexpected(families.error());
    }
    if (for_batch) {
        ResetInactiveBatchFamily(config, families->Linear);
    }
    if (const auto rejected = RejectInapplicableOptions(source, *families, for_batch); !rejected.has_value()) {
        return std::unexpected(rejected.error());
    }
    if (const auto applied = ApplySignalOptions(source, cli, config); !applied.has_value()) {
        return std::unexpected(applied.error());
    }
    if (const auto applied = ApplyDataSource(source, cli, config); !applied.has_value()) {
        return std::unexpected(applied.error());
    }
    if (const auto applied = ApplyAwgn(source, cli, config); !applied.has_value()) {
        return std::unexpected(applied.error());
    }
    ApplyImpairments(source, cli, config);
    if (const auto validated = Core::Validate(config); !validated.has_value()) {
        return std::unexpected(validated.error());
    }
    return config;
}

} // namespace

/**
 * @brief   Resolves the single-generation configuration as defaults -> loaded preset -> explicitly supplied options,
 *          then validates it through the shared library.
 * @param[in] cli  Parsed command line.
 * @return  The validated configuration; an error on unknown names or options incompatible with the selected
 *          waveform family.
 * @note    Options that were not supplied never replace preset values.
 */
std::expected<Core::GenerationConfig, std::string> ResolveConfig(const CommandLine &cli) {
    return ResolveBase(cli.App, cli, false);
}

/**
 * @brief   Maps a --format value to an export format.
 * @param[in] format  "csv" or "sigmf".
 * @return  The export format; an error on any other value.
 */
std::expected<Core::ExportFormat, std::string> ResolveFormat(const std::string &format) {
    if (format == "csv") {
        return Core::ExportFormat::CSV;
    }
    if (format == "sigmf") {
        return Core::ExportFormat::SigMF;
    }
    return std::unexpected("Unknown --format: " + format);
}

/**
 * @brief   Chooses the sample destination of a single generation.
 * @param[in] cli  Parsed command line.
 * @return  The explicit --output, else signal.csv or signal.sigmf-data by format; an error on an unknown format.
 */
std::expected<std::string, std::string> ResolveOutput(const CommandLine &cli) {
    if (!cli.Output.empty()) {
        return cli.Output;
    }
    const auto format = ResolveFormat(cli.Format);
    if (!format.has_value()) {
        return std::unexpected(format.error());
    }
    return *format == Core::ExportFormat::CSV ? "signal.csv" : "signal.sigmf-data";
}

/**
 * @brief   Resolves a batch request: the shared preset and overrides given after "batch", plus the sweep axes.
 * @param[in] cli  Parsed command line with the batch subcommand selected.
 * @return  The request, with a validated base configuration; an error on root options other than --log-level, on
 *          batch --overwrite, on unknown names and on options incompatible with the swept waveforms.
 */
std::expected<Core::BatchRequest, std::string> ResolveBatch(const CommandLine &cli) {
    // Root signal/export options belong to single generation. Accepting them here would silently discard values
    // because only subcommand counts are resolved
    for (const auto *option : cli.App.get_options()) {
        if (option->count() && option->get_name() != "--log-level") {
            return std::unexpected("With batch, put signal and format options after 'batch'; root option: " +
                                   option->get_name());
        }
    }
    if (const auto rejected = Reject(cli.BatchOverwrite, "batch --overwrite is rejected; choose a new --output-dir");
        !rejected.has_value()) {
        return std::unexpected(rejected.error());
    }
    Core::BatchRequest request;
    const auto base = ResolveBase(*cli.Batch, cli, true);
    if (!base.has_value()) {
        return std::unexpected(base.error());
    }
    request.Base = std::move(*base);
    for (const auto &name : cli.BatchModulations) {
        Core::Modulation modulation;
        if (!Core::ParseModulation(name, modulation)) {
            return std::unexpected("Unknown --modulations entry: " + name);
        }
        request.Waveforms.push_back(modulation);
    }
    request.Seeds = cli.BatchSeeds;
    request.SnrsDb = cli.BatchSnrsDb;
    request.FrameSize = cli.FrameSize;
    request.FramesPerPoint = cli.FramesPerPoint;
    const auto format = ResolveFormat(cli.BatchFormat);
    if (!format.has_value()) {
        return std::unexpected(format.error());
    }
    request.Format = *format;
    request.OutputDir = cli.OutputDir;
    return request;
}

/**
 * @brief   Resolves a BER sweep: the preset and signal options given after "ber", plus the Eb/N0 points and
 *          stopping rules.
 * @param[in] cli  Parsed command line with the ber subcommand selected.
 * @return  The request, or an error on root options other than --log-level, on --symbols, --bits, --data-source or
 *          --snr-db, on waveforms without a reference receiver and on out-of-range settings. The sweep sets the
 *          noise and draws random data itself, so AWGN is off, the data source is random and explicit bits are
 *          cleared. Without --eb-n0-db the points are 0 to 10 dB in steps of 2.
 */
std::expected<BerRequest, std::string> ResolveBer(const CommandLine &cli) {
    for (const auto *option : cli.App.get_options()) {
        if (option->count() && option->get_name() != "--log-level") {
            return std::unexpected("With ber, put signal options after 'ber'; root option: " + option->get_name());
        }
    }
    if (const auto rejected =
            Reject(Supplied(*cli.Ber, "--symbols") || Supplied(*cli.Ber, "--bits") ||
                       Supplied(*cli.Ber, "--data-source") || Supplied(*cli.Ber, "--snr-db"),
                   "ber rejects --symbols/--bits/--data-source/--snr-db; use --block-symbols and --eb-n0-db");
        !rejected.has_value()) {
        return std::unexpected(rejected.error());
    }
    BerRequest request;
    const auto config = ResolveBase(*cli.Ber, cli, false);
    if (!config.has_value()) {
        return std::unexpected(config.error());
    }
    request.Config = std::move(*config);
    request.Config.Awgn.Enabled = false; // The sweep sets the noise itself
    request.Config.DataSource = Core::DataSource::Random;
    request.Config.Bits.clear();
    if (!Core::HasReferenceDemodulator(request.Config.Modulation)) {
        return std::unexpected(std::string("ber needs a linear waveform; ") +
                               Core::ModulationName(request.Config.Modulation) + " has no reference receiver");
    }
    request.Settings.EbN0Db = cli.BerEbN0Db;
    if (request.Settings.EbN0Db.empty()) {
        for (int db = 0; db <= 10; db += 2) {
            request.Settings.EbN0Db.push_back(db);
        }
    }
    for (double db : request.Settings.EbN0Db) {
        if (const auto rejected =
                Reject(!std::isfinite(db) || std::abs(db) > 60, "--eb-n0-db values must be finite and within 60 dB");
            !rejected.has_value()) {
            return std::unexpected(rejected.error());
        }
    }
    if (const auto rejected = Reject(cli.BerMinErrors == 0, "--min-errors must be positive"); !rejected.has_value()) {
        return std::unexpected(rejected.error());
    }
    if (const auto rejected =
            Reject(cli.BerMaxBits == 0 || cli.BerMaxBits > 1000000000ULL, "--max-bits must be 1 to 1000000000");
        !rejected.has_value()) {
        return std::unexpected(rejected.error());
    }
    if (const auto rejected =
            Reject(cli.BerBlockSymbols < 64 || cli.BerBlockSymbols > 65536, "--block-symbols must be 64 to 65536");
        !rejected.has_value()) {
        return std::unexpected(rejected.error());
    }
    request.Settings.MinErrors = cli.BerMinErrors;
    request.Settings.MaxBits = cli.BerMaxBits;
    request.Settings.BlockSymbols = cli.BerBlockSymbols;
    return request;
}

/**
 * @brief   Maps a --window value to a Welch window.
 * @param[in] name  "hann", "hamming", "blackman" or "rectangular".
 * @return  The window; an error on any other value.
 */
std::expected<Core::Window, std::string> ResolveWindow(const std::string &name) {
    if (name == "hann") {
        return Core::Window::Hann;
    }
    if (name == "hamming") {
        return Core::Window::Hamming;
    }
    if (name == "blackman") {
        return Core::Window::Blackman;
    }
    if (name == "rectangular") {
        return Core::Window::Rectangular;
    }
    return std::unexpected("Unknown --window: " + name);
}

} // namespace Console
