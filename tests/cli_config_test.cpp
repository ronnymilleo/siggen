#include "cli_config.h"
#include "preset.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <vector>

namespace {
std::filesystem::path unique_path(const char* prefix) {
    return std::filesystem::temp_directory_path() /
           (std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}
}

TEST(CliConfig, RootOptionsCannotBeSilentlyDiscardedByBatch) {
    for (const auto& args : std::vector<std::vector<const char*>>{
             {"siggen", "--gain", "2", "batch"},
             {"siggen", "--modulation", "WGN", "batch"},
             {"siggen", "--preset", "missing.preset", "batch"},
             {"siggen", "--format", "csv", "batch"},
             {"siggen", "--overwrite", "batch"},
             {"siggen", "--output", "ignored.csv", "batch"},
             {"siggen", "--gain", "2", "batch", "--gain", "3"}}) {
        CommandLine cli("test");
        cli.app.parse(static_cast<int>(args.size()), args.data());
        EXPECT_THROW(resolve_batch(cli), std::invalid_argument);
    }
    CommandLine cli("test");
    const char* args[] = {"siggen", "--log-level", "off", "batch", "--gain", "2"};
    cli.app.parse(std::size(args), args);
    EXPECT_EQ(resolve_batch(cli).base.amplitude_gain, 2);
}

TEST(CliConfig, MixedBatchAcceptsApplicableOverridesForBothFamilies) {
    CommandLine cli("test");
    const char* args[] = {"siggen", "batch", "--modulations", "QPSK", "WGN",
                         "--sps", "4", "--noise-power", "2", "--sample-rate", "16000"};
    cli.app.parse(std::size(args), args);
    const auto r = resolve_batch(cli);
    EXPECT_EQ(r.base.samples_per_symbol, 4);
    EXPECT_EQ(r.base.noise_source.noise_power, 2);
    EXPECT_EQ(r.base.noise_source.sample_rate_hz, 16000);
}

TEST(CliConfig, DefaultsResolveToLegacyBehavior) {
    CommandLine cli("test");
    const char* argv[] = {"siggen"};
    cli.app.parse(std::size(argv), argv);
    EXPECT_EQ(resolve_config(cli), iq::GenerationConfig{});
    EXPECT_EQ(resolve_output(cli), "signal.csv");
    EXPECT_EQ(resolve_format(cli.format), iq::ExportFormat::CSV);
}

TEST(CliConfig, MixedBatchDoesNotActivateDormantPresetSettings) {
    const auto path = unique_path("iq-cli-mixed-preset-");
    iq::GenerationConfig preset;
    preset.modulation = iq::Modulation::WGN;
    preset.samples_per_symbol = 4; // Dormant linear setting must not become active.
    preset.noise_source.noise_power = 3;
    preset.amplitude_gain = 2;
    iq::save_preset(path, preset);
    const auto name = path.string();
    CommandLine cli("test");
    const char* args[] = {"siggen", "batch", "--preset", name.c_str(),
                         "--modulations", "QPSK", "WGN"};
    cli.app.parse(std::size(args), args);
    const auto request = resolve_batch(cli);
    EXPECT_EQ(request.base.samples_per_symbol, iq::GenerationConfig{}.samples_per_symbol);
    EXPECT_EQ(request.base.noise_source.noise_power, 3);
    EXPECT_EQ(request.base.amplitude_gain, 2);
    preset = {};
    preset.noise_source.noise_power = 9; // Dormant noise setting must reset too.
    iq::save_preset(path, preset);
    CommandLine linear("test");
    linear.app.parse(std::size(args), args);
    EXPECT_EQ(resolve_batch(linear).base.noise_source.noise_power, 1);
    std::filesystem::remove(path);
}

TEST(CliConfig, PrecedenceDefaultsPresetThenExplicitOptions) {
    const auto path = unique_path("iq-cli-preset-");
    iq::GenerationConfig preset_config;
    preset_config.modulation = iq::Modulation::QPSK;
    preset_config.symbol_count = 100;
    preset_config.amplitude_gain = 3;
    preset_config.seed = 77;
    iq::save_preset(path, preset_config);
    const auto path_str = path.string();

    {
        CommandLine cli("test");
        const char* argv[] = {"siggen", "--preset", path_str.c_str()};
        cli.app.parse(std::size(argv), argv);
        EXPECT_EQ(resolve_config(cli), preset_config);
    }
    {
        // Explicitly supplied options override the preset; unsupplied ones do not.
        CommandLine cli("test");
        const char* argv[] = {"siggen", "--preset", path_str.c_str(), "--gain", "5"};
        cli.app.parse(std::size(argv), argv);
        auto resolved = resolve_config(cli);
        EXPECT_DOUBLE_EQ(resolved.amplitude_gain, 5);
        EXPECT_EQ(resolved.modulation, iq::Modulation::QPSK);
        EXPECT_EQ(resolved.symbol_count, 100);
        EXPECT_EQ(resolved.seed, 77u);
    }
    {
        CommandLine cli("test");
        const char* argv[] = {"siggen", "--preset", path_str.c_str(), "--symbols", "32", "--sps", "4",
                              "--span", "4"};
        cli.app.parse(std::size(argv), argv);
        auto resolved = resolve_config(cli);
        EXPECT_EQ(resolved.symbol_count, 32);
        EXPECT_EQ(resolved.samples_per_symbol, 4);
        EXPECT_EQ(resolved.span_symbols, 4);
    }
    std::filesystem::remove(path);
}

TEST(CliConfig, FamilySwitchStartsFromFamilyDefaultsRetainingGainAndSeeds) {
    const auto path = unique_path("iq-cli-family-");
    iq::GenerationConfig preset_config;
    preset_config.symbol_count = 100;
    preset_config.amplitude_gain = 3;
    preset_config.seed = 77;
    preset_config.noise_seed = 88;
    iq::save_preset(path, preset_config);
    const auto path_str = path.string();
    CommandLine cli("test");
    const char* argv[] = {"siggen", "--preset", path_str.c_str(), "--modulation", "wgn"};
    cli.app.parse(std::size(argv), argv);
    const auto resolved = resolve_config(cli);
    EXPECT_EQ(resolved.modulation, iq::Modulation::WGN);
    EXPECT_DOUBLE_EQ(resolved.amplitude_gain, 3);
    EXPECT_EQ(resolved.seed, 77u);
    EXPECT_EQ(resolved.noise_seed, 88u);
    // Inactive preset fields do not become active: noise source uses family defaults.
    EXPECT_EQ(resolved.noise_source, iq::NoiseSourceSettings{});
    std::filesystem::remove(path);

    // And back: a WGN preset switching to linear uses linear defaults.
    const auto wgn_path = unique_path("iq-cli-family-wgn-");
    iq::GenerationConfig wgn;
    wgn.modulation = iq::Modulation::WGN;
    wgn.noise_source.sample_count = 512;
    wgn.amplitude_gain = 2;
    iq::save_preset(wgn_path, wgn);
    const auto wgn_path_str = wgn_path.string();
    CommandLine back("test");
    const char* back_argv[] = {"siggen", "--preset", wgn_path_str.c_str(), "--modulation", "64-QAM"};
    back.app.parse(std::size(back_argv), back_argv);
    const auto linear = resolve_config(back);
    EXPECT_EQ(linear.modulation, iq::Modulation::QAM64);
    EXPECT_DOUBLE_EQ(linear.amplitude_gain, 2);
    EXPECT_EQ(linear.symbol_count, 256);
    // Incompatible preset fields do not become active: family defaults are used.
    EXPECT_EQ(linear.noise_source, iq::NoiseSourceSettings{});
    std::filesystem::remove(wgn_path);
}

TEST(CliConfig, WaveformAndPulseNamesAreCaseInsensitive) {
    CommandLine cli("test");
    const char* argv[] = {"siggen", "--modulation", "8-psk", "--pulse", "rrc"};
    cli.app.parse(std::size(argv), argv);
    const auto resolved = resolve_config(cli);
    EXPECT_EQ(resolved.modulation, iq::Modulation::PSK8);
    EXPECT_EQ(resolved.pulse, iq::Pulse::RRC);
    CommandLine bad("test");
    const char* bad_argv[] = {"siggen", "--modulation", "GFSK"};
    bad.app.parse(std::size(bad_argv), bad_argv);
    EXPECT_THROW(resolve_config(bad), std::invalid_argument);
}

TEST(CliConfig, BitsSelectExplicitInputUnlessConflicting) {
    CommandLine cli("test");
    const char* argv[] = {"siggen", "--symbols", "4", "--bits", "0101"};
    cli.app.parse(std::size(argv), argv);
    const auto resolved = resolve_config(cli);
    EXPECT_EQ(resolved.data_source, iq::DataSource::Explicit);
    EXPECT_EQ(resolved.bits, "0101");

    CommandLine explicit_source("test");
    const char* explicit_argv[] = {"siggen", "--symbols", "4", "--data-source", "explicit", "--bits", "0101"};
    explicit_source.app.parse(std::size(explicit_argv), explicit_argv);
    EXPECT_EQ(resolve_config(explicit_source).data_source, iq::DataSource::Explicit);

    CommandLine conflict("test");
    const char* conflict_argv[] = {"siggen", "--symbols", "4", "--data-source", "random", "--bits", "0101"};
    conflict.app.parse(std::size(conflict_argv), conflict_argv);
    EXPECT_THROW(resolve_config(conflict), std::invalid_argument);

    // Exact bit-count validation is preserved through the CLI.
    CommandLine wrong_count("test");
    const char* wrong_argv[] = {"siggen", "--symbols", "4", "--bits", "010"};
    wrong_count.app.parse(std::size(wrong_argv), wrong_argv);
    EXPECT_THROW(resolve_config(wrong_count), std::invalid_argument);
}

TEST(CliConfig, IncompatibleOptionsAreRejected) {
    const struct {
        const char* description;
        std::vector<const char*> args;
    } cases[] = {
        {"linear option on WGN", {"siggen", "--modulation", "WGN", "--symbols", "10"}},
        {"pulse on WGN", {"siggen", "--modulation", "WGN", "--pulse", "rrc"}},
        {"AWGN on WGN", {"siggen", "--modulation", "WGN", "--snr-db", "10"}},
        {"bits on WGN", {"siggen", "--modulation", "WGN", "--bits", "0"}},
        {"noise source on linear", {"siggen", "--samples", "10"}},
        {"noise power on linear", {"siggen", "--modulation", "QPSK", "--noise-power", "2"}},
    };
    for (const auto& test_case : cases) {
        CommandLine cli("test");
        cli.app.parse(static_cast<int>(test_case.args.size()), test_case.args.data());
        EXPECT_THROW(resolve_config(cli), std::invalid_argument) << test_case.description;
    }
}

TEST(CliConfig, SnrControl) {
    CommandLine enabled("test");
    const char* enabled_argv[] = {"siggen", "--snr-db", "7.5"};
    enabled.app.parse(std::size(enabled_argv), enabled_argv);
    const auto on = resolve_config(enabled);
    EXPECT_TRUE(on.awgn.enabled);
    EXPECT_DOUBLE_EQ(on.awgn.snr_db, 7.5);

    // "off" disables preset-provided AWGN.
    const auto path = unique_path("iq-cli-snr-");
    iq::GenerationConfig preset_config;
    preset_config.awgn.enabled = true;
    preset_config.awgn.snr_db = 3;
    iq::save_preset(path, preset_config);
    const auto path_str = path.string();
    CommandLine off("test");
    const char* off_argv[] = {"siggen", "--preset", path_str.c_str(), "--snr-db", "off"};
    off.app.parse(std::size(off_argv), off_argv);
    EXPECT_FALSE(resolve_config(off).awgn.enabled);
    CommandLine invalid("test");
    const char* invalid_argv[] = {"siggen", "--snr-db", "loud"};
    invalid.app.parse(std::size(invalid_argv), invalid_argv);
    EXPECT_THROW(resolve_config(invalid), std::invalid_argument);
    std::filesystem::remove(path);
}

TEST(CliConfig, WgnSourceOptions) {
    CommandLine cli("test");
    const char* argv[] = {"siggen", "--modulation", "wgn", "--samples", "512", "--sample-rate", "16000",
                          "--noise-power", "2", "--noise-seed", "21", "--gain", "1.5", "--format", "sigmf"};
    cli.app.parse(std::size(argv), argv);
    const auto resolved = resolve_config(cli);
    EXPECT_EQ(resolved.modulation, iq::Modulation::WGN);
    EXPECT_EQ(resolved.noise_source.sample_count, 512);
    EXPECT_DOUBLE_EQ(resolved.noise_source.sample_rate_hz, 16000);
    EXPECT_DOUBLE_EQ(resolved.noise_source.noise_power, 2);
    EXPECT_EQ(resolved.noise_seed, 21u);
    EXPECT_EQ(resolve_output(cli), "signal.sigmf-data");
    CommandLine named("test");
    const char* named_argv[] = {"siggen", "--format", "sigmf", "--output", "custom.dat"};
    named.app.parse(std::size(named_argv), named_argv);
    EXPECT_EQ(resolve_output(named), "custom.dat");
}

TEST(CliConfig, BatchResolutionSharesOverridesAndRejectsExplicitInput) {
    CommandLine cli("test");
    const char* argv[] = {"siggen", "batch", "--modulations", "bpsk", "WGN", "--seeds", "1", "2",
                          "--snrs-db=0,10", "--frame-size", "64", "--frames-per-point", "2",
                          "--output-dir", "ds", "--format", "csv", "--gain", "2", "--modulation", "QPSK",
                          "--sps", "4", "--span", "4", "--snr-db", "5"};
    cli.app.parse(std::size(argv), argv);
    const auto request = resolve_batch(cli);
    EXPECT_EQ(request.waveforms, (std::vector<iq::Modulation>{iq::Modulation::BPSK, iq::Modulation::WGN}));
    EXPECT_EQ(request.seeds, (std::vector<std::uint32_t>{1, 2}));
    EXPECT_EQ(request.snrs_db, (std::vector<double>{0, 10}));
    EXPECT_EQ(request.frame_size, 64);
    EXPECT_EQ(request.frames_per_point, 2);
    EXPECT_EQ(request.output_dir, "ds");
    EXPECT_EQ(request.format, iq::ExportFormat::CSV);
    EXPECT_DOUBLE_EQ(request.base.amplitude_gain, 2);
    EXPECT_EQ(request.base.modulation, iq::Modulation::QPSK);
    EXPECT_EQ(request.base.samples_per_symbol, 4);
    EXPECT_TRUE(request.base.awgn.enabled);
    EXPECT_DOUBLE_EQ(request.base.awgn.snr_db, 5);

    for (const auto& rejected_case : std::vector<std::vector<const char*>>{
             {"siggen", "batch", "--symbols", "4"}, {"siggen", "batch", "--samples", "4"},
             {"siggen", "batch", "--bits", "0101"}, {"siggen", "batch", "--data-source", "random"}}) {
        CommandLine rejected("test");
        rejected.app.parse(static_cast<int>(rejected_case.size()), rejected_case.data());
        EXPECT_THROW(resolve_batch(rejected), std::invalid_argument) << rejected_case[2];
    }
    CommandLine overwrite("test");
    const char* overwrite_argv[] = {"siggen", "batch", "--overwrite"};
    overwrite.app.parse(std::size(overwrite_argv), overwrite_argv);
    EXPECT_THROW(resolve_batch(overwrite), std::invalid_argument);
    CommandLine unknown("test");
    const char* unknown_argv[] = {"siggen", "batch", "--modulations", "GFSK"};
    unknown.app.parse(std::size(unknown_argv), unknown_argv);
    EXPECT_THROW(resolve_batch(unknown), std::invalid_argument);
}

TEST(CliConfig, BatchDefaultsUseResolvedWaveformAndSeed) {
    CommandLine cli("test");
    const char* argv[] = {"siggen", "batch", "--modulation", "8-PSK", "--seed", "123"};
    cli.app.parse(std::size(argv), argv);
    const auto request = resolve_batch(cli);
    EXPECT_TRUE(request.waveforms.empty());
    EXPECT_TRUE(request.seeds.empty());
    EXPECT_TRUE(request.snrs_db.empty());
    EXPECT_EQ(request.base.modulation, iq::Modulation::PSK8);
    EXPECT_EQ(request.base.seed, 123u);
    EXPECT_EQ(request.frame_size, 2048);
    EXPECT_EQ(request.frames_per_point, 1);
    EXPECT_EQ(request.format, iq::ExportFormat::SigMF);
}

TEST(CliConfig, ImpairmentOptionsResolveAndRejectWgn) {
    CommandLine cli("test");
    const char* args[] = {"siggen", "--modulation", "QPSK", "--cfo-hz", "12.5", "--phase-noise-hz", "2",
                         "--iq-gain-db", "1", "--iq-phase-deg", "3", "--dc-i", "0.1", "--dc-q", "-0.1",
                         "--adc-bits", "5", "--impairment-seed", "77"};
    cli.app.parse(std::size(args), args);
    const auto c = resolve_config(cli);
    EXPECT_EQ(c.impairments.cfo_hz, 12.5);
    EXPECT_EQ(c.impairments.phase_noise_linewidth_hz, 2);
    EXPECT_EQ(c.impairments.iq_gain_db, 1);
    EXPECT_EQ(c.impairments.iq_phase_deg, 3);
    EXPECT_EQ(c.impairments.dc_offset_i, 0.1);
    EXPECT_EQ(c.impairments.dc_offset_q, -0.1);
    EXPECT_EQ(c.impairments.adc_bits, 5);
    EXPECT_EQ(c.impairment_seed, 77u);
    CommandLine wgn("test");
    const char* bad[] = {"siggen", "--modulation", "WGN", "--cfo-hz", "5"};
    wgn.app.parse(std::size(bad), bad);
    EXPECT_THROW(resolve_config(wgn), std::invalid_argument);
    CommandLine batch("test");
    const char* b[] = {"siggen", "batch", "--cfo-hz", "5", "--adc-bits", "8"};
    batch.app.parse(std::size(b), b);
    const auto r = resolve_batch(batch);
    EXPECT_EQ(r.base.impairments.cfo_hz, 5);
    EXPECT_EQ(r.base.impairments.adc_bits, 8);
}

TEST(CliConfig, FskOptionsResolveAndRejectInapplicableOnes) {
    CommandLine cli("test");
    const char* args[] = {"siggen", "--modulation", "4-fsk", "--tone-spacing-hz", "600", "--symbol-rate", "1000", "--sps", "8"};
    cli.app.parse(std::size(args), args);
    const auto c = resolve_config(cli);
    EXPECT_EQ(c.modulation, iq::Modulation::FSK4);
    EXPECT_EQ(c.tone_spacing_hz, 600);
    for (const auto& bad : std::vector<std::vector<const char*>>{
             {"siggen", "--modulation", "MSK", "--tone-spacing-hz", "600"},
             {"siggen", "--modulation", "QPSK", "--tone-spacing-hz", "600"},
             {"siggen", "--modulation", "2-FSK", "--pulse", "rrc"},
             {"siggen", "--modulation", "MSK", "--roll-off", "0.3"},
             {"siggen", "--modulation", "4-FSK", "--sps", "1"}}) {
        CommandLine rejected("test");
        rejected.app.parse(static_cast<int>(bad.size()), bad.data());
        EXPECT_THROW(resolve_config(rejected), std::invalid_argument);
    }
    CommandLine msk("test");
    const char* ok[] = {"siggen", "--modulation", "msk", "--snr-db", "12", "--cfo-hz", "3"};
    msk.app.parse(std::size(ok), ok);
    const auto m = resolve_config(msk);
    EXPECT_EQ(m.modulation, iq::Modulation::MSK);
    EXPECT_TRUE(m.awgn.enabled);
    CommandLine batch("test");
    const char* mixed[] = {"siggen", "batch", "--modulations", "QPSK", "2-FSK", "--tone-spacing-hz", "800", "--sps", "8"};
    batch.app.parse(std::size(mixed), mixed);
    EXPECT_EQ(resolve_batch(batch).base.tone_spacing_hz, 800);
}

TEST(CliConfig, BerResolvesPointsAndStoppingRules) {
    CommandLine cli("test");
    const char* args[] = {"siggen", "ber", "--modulation", "16-QAM", "--sps", "4", "--eb-n0-db=2,4,6", "--min-errors", "50",
                          "--max-bits", "5000", "--block-symbols", "256"};
    cli.app.parse(static_cast<int>(std::size(args)), args);
    ASSERT_TRUE(cli.ber_selected());
    const auto request = resolve_ber(cli);
    EXPECT_EQ(request.config.modulation, iq::Modulation::QAM16);
    EXPECT_EQ(request.config.samples_per_symbol, 4);
    EXPECT_FALSE(request.config.awgn.enabled);
    EXPECT_EQ(request.settings.eb_n0_db, (std::vector<double>{2, 4, 6}));
    EXPECT_EQ(request.settings.min_errors, 50u);
    EXPECT_EQ(request.settings.max_bits, 5000u);
    EXPECT_EQ(request.settings.block_symbols, 256);
}

TEST(CliConfig, BerDefaultsAndRejections) {
    {
        CommandLine cli("test");
        const char* args[] = {"siggen", "ber"};
        cli.app.parse(static_cast<int>(std::size(args)), args);
        EXPECT_EQ(resolve_ber(cli).settings.eb_n0_db.size(), 6u);
    }
    for (const auto& args : std::vector<std::vector<const char*>>{
             {"siggen", "ber", "--modulation", "WGN"}, {"siggen", "ber", "--modulation", "MSK"},
             {"siggen", "ber", "--snr-db", "5"}, {"siggen", "ber", "--bits", "1010"}, {"siggen", "ber", "--symbols", "100"},
             {"siggen", "ber", "--min-errors", "0"}, {"siggen", "ber", "--block-symbols", "8"},
             {"siggen", "--gain", "2", "ber"}}) {
        CommandLine cli("test");
        cli.app.parse(static_cast<int>(args.size()), args.data());
        EXPECT_THROW(resolve_ber(cli), std::invalid_argument);
    }
}
