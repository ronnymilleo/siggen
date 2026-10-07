/**
 * @file    cli_config_test.cpp
 * @brief   Tests for resolving command-line options into generation configs and batch requests.
 */

#include "cli_config.h"
#include "preset.h"
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <iterator>
#include <vector>

namespace Console {

namespace {

std::filesystem::path UniquePath(const char *prefix) {
    return std::filesystem::temp_directory_path() /
           (std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

} // namespace

TEST(CliConfig, RootOptionsCannotBeSilentlyDiscardedByBatch) {
    for (const auto &args : std::vector<std::vector<const char *>>{{"siggen", "--gain", "2", "batch"},
                                                                   {"siggen", "--modulation", "WGN", "batch"},
                                                                   {"siggen", "--preset", "missing.preset", "batch"},
                                                                   {"siggen", "--format", "csv", "batch"},
                                                                   {"siggen", "--overwrite", "batch"},
                                                                   {"siggen", "--output", "ignored.csv", "batch"},
                                                                   {"siggen", "--gain", "2", "batch", "--gain", "3"}}) {
        CommandLine cli("test");
        cli.App.parse(static_cast<int>(args.size()), args.data());
        EXPECT_THROW(ResolveBatch(cli), std::invalid_argument);
    }
    CommandLine cli("test");
    const char *args[] = {"siggen", "--log-level", "off", "batch", "--gain", "2"};
    cli.App.parse(std::size(args), args);
    EXPECT_EQ(ResolveBatch(cli).Base.AmplitudeGain, 2);
}

TEST(CliConfig, MixedBatchAcceptsApplicableOverridesForBothFamilies) {
    CommandLine cli("test");
    const char *args[] = {"siggen",        "batch", "--modulations", "QPSK", "WGN", "--sps", "4",
                          "--noise-power", "2",     "--sample-rate", "16000"};
    cli.App.parse(std::size(args), args);
    const auto r = ResolveBatch(cli);
    EXPECT_EQ(r.Base.SamplesPerSymbol, 4);
    EXPECT_EQ(r.Base.NoiseSource.NoisePower, 2);
    EXPECT_EQ(r.Base.NoiseSource.SampleRateHz, 16000);
}

TEST(CliConfig, DefaultsResolveToLegacyBehavior) {
    CommandLine cli("test");
    const char *argv[] = {"siggen"};
    cli.App.parse(std::size(argv), argv);
    EXPECT_EQ(ResolveConfig(cli), Core::GenerationConfig{});
    EXPECT_EQ(ResolveOutput(cli), "signal.csv");
    EXPECT_EQ(ResolveFormat(cli.Format), Core::ExportFormat::CSV);
}

TEST(CliConfig, MixedBatchDoesNotActivateDormantPresetSettings) {
    const auto path = UniquePath("iq-cli-mixed-preset-");
    Core::GenerationConfig preset;
    preset.Modulation = Core::Modulation::WGN;
    preset.SamplesPerSymbol = 4; // Dormant linear setting must not become active.
    preset.NoiseSource.NoisePower = 3;
    preset.AmplitudeGain = 2;
    Core::SavePreset(path, preset);
    const auto name = path.string();
    CommandLine cli("test");
    const char *args[] = {"siggen", "batch", "--preset", name.c_str(), "--modulations", "QPSK", "WGN"};
    cli.App.parse(std::size(args), args);
    const auto request = ResolveBatch(cli);
    EXPECT_EQ(request.Base.SamplesPerSymbol, Core::GenerationConfig{}.SamplesPerSymbol);
    EXPECT_EQ(request.Base.NoiseSource.NoisePower, 3);
    EXPECT_EQ(request.Base.AmplitudeGain, 2);
    preset = {};
    preset.NoiseSource.NoisePower = 9; // Dormant noise setting must reset too.
    Core::SavePreset(path, preset);
    CommandLine linear("test");
    linear.App.parse(std::size(args), args);
    EXPECT_EQ(ResolveBatch(linear).Base.NoiseSource.NoisePower, 1);
    std::filesystem::remove(path);
}

TEST(CliConfig, PrecedenceDefaultsPresetThenExplicitOptions) {
    const auto path = UniquePath("iq-cli-preset-");
    Core::GenerationConfig preset_config;
    preset_config.Modulation = Core::Modulation::QPSK;
    preset_config.SymbolCount = 100;
    preset_config.AmplitudeGain = 3;
    preset_config.Seed = 77;
    Core::SavePreset(path, preset_config);
    const auto path_str = path.string();

    {
        CommandLine cli("test");
        const char *argv[] = {"siggen", "--preset", path_str.c_str()};
        cli.App.parse(std::size(argv), argv);
        EXPECT_EQ(ResolveConfig(cli), preset_config);
    }
    {
        // Explicitly supplied options override the preset; unsupplied ones do not.
        CommandLine cli("test");
        const char *argv[] = {"siggen", "--preset", path_str.c_str(), "--gain", "5"};
        cli.App.parse(std::size(argv), argv);
        auto resolved = ResolveConfig(cli);
        EXPECT_DOUBLE_EQ(resolved.AmplitudeGain, 5);
        EXPECT_EQ(resolved.Modulation, Core::Modulation::QPSK);
        EXPECT_EQ(resolved.SymbolCount, 100);
        EXPECT_EQ(resolved.Seed, 77u);
    }
    {
        CommandLine cli("test");
        const char *argv[] = {"siggen", "--preset", path_str.c_str(), "--symbols", "32", "--sps", "4", "--span", "4"};
        cli.App.parse(std::size(argv), argv);
        auto resolved = ResolveConfig(cli);
        EXPECT_EQ(resolved.SymbolCount, 32);
        EXPECT_EQ(resolved.SamplesPerSymbol, 4);
        EXPECT_EQ(resolved.SpanSymbols, 4);
    }
    std::filesystem::remove(path);
}

TEST(CliConfig, FamilySwitchStartsFromFamilyDefaultsRetainingGainAndSeeds) {
    const auto path = UniquePath("iq-cli-family-");
    Core::GenerationConfig preset_config;
    preset_config.SymbolCount = 100;
    preset_config.AmplitudeGain = 3;
    preset_config.Seed = 77;
    preset_config.NoiseSeed = 88;
    Core::SavePreset(path, preset_config);
    const auto path_str = path.string();
    CommandLine cli("test");
    const char *argv[] = {"siggen", "--preset", path_str.c_str(), "--modulation", "wgn"};
    cli.App.parse(std::size(argv), argv);
    const auto resolved = ResolveConfig(cli);
    EXPECT_EQ(resolved.Modulation, Core::Modulation::WGN);
    EXPECT_DOUBLE_EQ(resolved.AmplitudeGain, 3);
    EXPECT_EQ(resolved.Seed, 77u);
    EXPECT_EQ(resolved.NoiseSeed, 88u);
    // Inactive preset fields do not become active: noise source uses family defaults.
    EXPECT_EQ(resolved.NoiseSource, Core::NoiseSourceSettings{});
    std::filesystem::remove(path);

    // And back: a WGN preset switching to linear uses linear defaults.
    const auto wgn_path = UniquePath("iq-cli-family-wgn-");
    Core::GenerationConfig wgn;
    wgn.Modulation = Core::Modulation::WGN;
    wgn.NoiseSource.SampleCount = 512;
    wgn.AmplitudeGain = 2;
    Core::SavePreset(wgn_path, wgn);
    const auto wgn_path_str = wgn_path.string();
    CommandLine back("test");
    const char *back_argv[] = {"siggen", "--preset", wgn_path_str.c_str(), "--modulation", "64-QAM"};
    back.App.parse(std::size(back_argv), back_argv);
    const auto linear = ResolveConfig(back);
    EXPECT_EQ(linear.Modulation, Core::Modulation::QAM64);
    EXPECT_DOUBLE_EQ(linear.AmplitudeGain, 2);
    EXPECT_EQ(linear.SymbolCount, 256);
    // Incompatible preset fields do not become active: family defaults are used.
    EXPECT_EQ(linear.NoiseSource, Core::NoiseSourceSettings{});
    std::filesystem::remove(wgn_path);
}

TEST(CliConfig, WaveformAndPulseNamesAreCaseInsensitive) {
    CommandLine cli("test");
    const char *argv[] = {"siggen", "--modulation", "8-psk", "--pulse", "rrc"};
    cli.App.parse(std::size(argv), argv);
    const auto resolved = ResolveConfig(cli);
    EXPECT_EQ(resolved.Modulation, Core::Modulation::PSK8);
    EXPECT_EQ(resolved.Pulse, Core::Pulse::RRC);
    CommandLine bad("test");
    const char *bad_argv[] = {"siggen", "--modulation", "GFSK"};
    bad.App.parse(std::size(bad_argv), bad_argv);
    EXPECT_THROW(ResolveConfig(bad), std::invalid_argument);
}

TEST(CliConfig, BitsSelectExplicitInputUnlessConflicting) {
    CommandLine cli("test");
    const char *argv[] = {"siggen", "--symbols", "4", "--bits", "0101"};
    cli.App.parse(std::size(argv), argv);
    const auto resolved = ResolveConfig(cli);
    EXPECT_EQ(resolved.DataSource, Core::DataSource::Explicit);
    EXPECT_EQ(resolved.Bits, "0101");

    CommandLine explicit_source("test");
    const char *explicit_argv[] = {"siggen", "--symbols", "4", "--data-source", "explicit", "--bits", "0101"};
    explicit_source.App.parse(std::size(explicit_argv), explicit_argv);
    EXPECT_EQ(ResolveConfig(explicit_source).DataSource, Core::DataSource::Explicit);

    CommandLine conflict("test");
    const char *conflict_argv[] = {"siggen", "--symbols", "4", "--data-source", "random", "--bits", "0101"};
    conflict.App.parse(std::size(conflict_argv), conflict_argv);
    EXPECT_THROW(ResolveConfig(conflict), std::invalid_argument);

    // Exact bit-count validation is preserved through the CLI.
    CommandLine wrong_count("test");
    const char *wrong_argv[] = {"siggen", "--symbols", "4", "--bits", "010"};
    wrong_count.App.parse(std::size(wrong_argv), wrong_argv);
    EXPECT_THROW(ResolveConfig(wrong_count), std::invalid_argument);
}

TEST(CliConfig, IncompatibleOptionsAreRejected) {
    const struct {
        const char *Description;
        std::vector<const char *> Args;
    } cases[] = {
        {"linear option on WGN", {"siggen", "--modulation", "WGN", "--symbols", "10"}},
        {"pulse on WGN", {"siggen", "--modulation", "WGN", "--pulse", "rrc"}},
        {"AWGN on WGN", {"siggen", "--modulation", "WGN", "--snr-db", "10"}},
        {"bits on WGN", {"siggen", "--modulation", "WGN", "--bits", "0"}},
        {"noise source on linear", {"siggen", "--samples", "10"}},
        {"noise power on linear", {"siggen", "--modulation", "QPSK", "--noise-power", "2"}},
    };
    for (const auto &test_case : cases) {
        CommandLine cli("test");
        cli.App.parse(static_cast<int>(test_case.Args.size()), test_case.Args.data());
        EXPECT_THROW(ResolveConfig(cli), std::invalid_argument) << test_case.Description;
    }
}

TEST(CliConfig, SnrControl) {
    CommandLine enabled("test");
    const char *enabled_argv[] = {"siggen", "--snr-db", "7.5"};
    enabled.App.parse(std::size(enabled_argv), enabled_argv);
    const auto on = ResolveConfig(enabled);
    EXPECT_TRUE(on.Awgn.Enabled);
    EXPECT_DOUBLE_EQ(on.Awgn.SnrDb, 7.5);

    // "off" disables preset-provided AWGN.
    const auto path = UniquePath("iq-cli-snr-");
    Core::GenerationConfig preset_config;
    preset_config.Awgn.Enabled = true;
    preset_config.Awgn.SnrDb = 3;
    Core::SavePreset(path, preset_config);
    const auto path_str = path.string();
    CommandLine off("test");
    const char *off_argv[] = {"siggen", "--preset", path_str.c_str(), "--snr-db", "off"};
    off.App.parse(std::size(off_argv), off_argv);
    EXPECT_FALSE(ResolveConfig(off).Awgn.Enabled);
    CommandLine invalid("test");
    const char *invalid_argv[] = {"siggen", "--snr-db", "loud"};
    invalid.App.parse(std::size(invalid_argv), invalid_argv);
    EXPECT_THROW(ResolveConfig(invalid), std::invalid_argument);
    std::filesystem::remove(path);
}

TEST(CliConfig, WgnSourceOptions) {
    CommandLine cli("test");
    const char *argv[] = {"siggen", "--modulation",  "wgn", "--samples",    "512", "--sample-rate",
                          "16000",  "--noise-power", "2",   "--noise-seed", "21",  "--gain",
                          "1.5",    "--format",      "cf32"};
    cli.App.parse(std::size(argv), argv);
    const auto resolved = ResolveConfig(cli);
    EXPECT_EQ(resolved.Modulation, Core::Modulation::WGN);
    EXPECT_EQ(resolved.NoiseSource.SampleCount, 512);
    EXPECT_DOUBLE_EQ(resolved.NoiseSource.SampleRateHz, 16000);
    EXPECT_DOUBLE_EQ(resolved.NoiseSource.NoisePower, 2);
    EXPECT_EQ(resolved.NoiseSeed, 21u);
    EXPECT_EQ(ResolveOutput(cli), "signal.iq");
    CommandLine named("test");
    const char *named_argv[] = {"siggen", "--format", "cf32", "--output", "custom.dat"};
    named.App.parse(std::size(named_argv), named_argv);
    EXPECT_EQ(ResolveOutput(named), "custom.dat");
}

TEST(CliConfig, BatchResolutionSharesOverridesAndRejectsExplicitInput) {
    CommandLine cli("test");
    const char *argv[] = {"siggen",
                          "batch",
                          "--modulations",
                          "bpsk",
                          "WGN",
                          "--seeds",
                          "1",
                          "2",
                          "--snrs-db=0,10",
                          "--frame-size",
                          "64",
                          "--frames-per-point",
                          "2",
                          "--output-dir",
                          "ds",
                          "--format",
                          "csv",
                          "--gain",
                          "2",
                          "--modulation",
                          "QPSK",
                          "--sps",
                          "4",
                          "--span",
                          "4",
                          "--snr-db",
                          "5"};
    cli.App.parse(std::size(argv), argv);
    const auto request = ResolveBatch(cli);
    EXPECT_EQ(request.Waveforms, (std::vector<Core::Modulation>{Core::Modulation::BPSK, Core::Modulation::WGN}));
    EXPECT_EQ(request.Seeds, (std::vector<std::uint32_t>{1, 2}));
    EXPECT_EQ(request.SnrsDb, (std::vector<double>{0, 10}));
    EXPECT_EQ(request.FrameSize, 64);
    EXPECT_EQ(request.FramesPerPoint, 2);
    EXPECT_EQ(request.OutputDir, "ds");
    EXPECT_EQ(request.Format, Core::ExportFormat::CSV);
    EXPECT_DOUBLE_EQ(request.Base.AmplitudeGain, 2);
    EXPECT_EQ(request.Base.Modulation, Core::Modulation::QPSK);
    EXPECT_EQ(request.Base.SamplesPerSymbol, 4);
    EXPECT_TRUE(request.Base.Awgn.Enabled);
    EXPECT_DOUBLE_EQ(request.Base.Awgn.SnrDb, 5);

    for (const auto &rejected_case :
         std::vector<std::vector<const char *>>{{"siggen", "batch", "--symbols", "4"},
                                                {"siggen", "batch", "--samples", "4"},
                                                {"siggen", "batch", "--bits", "0101"},
                                                {"siggen", "batch", "--data-source", "random"}}) {
        CommandLine rejected("test");
        rejected.App.parse(static_cast<int>(rejected_case.size()), rejected_case.data());
        EXPECT_THROW(ResolveBatch(rejected), std::invalid_argument) << rejected_case[2];
    }
    CommandLine overwrite("test");
    const char *overwrite_argv[] = {"siggen", "batch", "--overwrite"};
    overwrite.App.parse(std::size(overwrite_argv), overwrite_argv);
    EXPECT_THROW(ResolveBatch(overwrite), std::invalid_argument);
    CommandLine unknown("test");
    const char *unknown_argv[] = {"siggen", "batch", "--modulations", "GFSK"};
    unknown.App.parse(std::size(unknown_argv), unknown_argv);
    EXPECT_THROW(ResolveBatch(unknown), std::invalid_argument);
}

TEST(CliConfig, BatchDefaultsUseResolvedWaveformAndSeed) {
    CommandLine cli("test");
    const char *argv[] = {"siggen", "batch", "--modulation", "8-PSK", "--seed", "123"};
    cli.App.parse(std::size(argv), argv);
    const auto request = ResolveBatch(cli);
    EXPECT_TRUE(request.Waveforms.empty());
    EXPECT_TRUE(request.Seeds.empty());
    EXPECT_TRUE(request.SnrsDb.empty());
    EXPECT_EQ(request.Base.Modulation, Core::Modulation::PSK8);
    EXPECT_EQ(request.Base.Seed, 123u);
    EXPECT_EQ(request.FrameSize, 2048);
    EXPECT_EQ(request.FramesPerPoint, 1);
    EXPECT_EQ(request.Format, Core::ExportFormat::BinaryFloat32);
}

TEST(CliConfig, ImpairmentOptionsResolveAndRejectWgn) {
    CommandLine cli("test");
    const char *args[] = {"siggen", "--modulation", "QPSK", "--cfo-hz",       "12.5", "--phase-noise-hz",
                          "2",      "--iq-gain-db", "1",    "--iq-phase-deg", "3",    "--dc-i",
                          "0.1",    "--dc-q",       "-0.1", "--adc-bits",     "5",    "--impairment-seed",
                          "77"};
    cli.App.parse(std::size(args), args);
    const auto c = ResolveConfig(cli);
    EXPECT_EQ(c.Impairments.CfoHz, 12.5);
    EXPECT_EQ(c.Impairments.PhaseNoiseLinewidthHz, 2);
    EXPECT_EQ(c.Impairments.IqGainDb, 1);
    EXPECT_EQ(c.Impairments.IqPhaseDeg, 3);
    EXPECT_EQ(c.Impairments.DcOffsetI, 0.1);
    EXPECT_EQ(c.Impairments.DcOffsetQ, -0.1);
    EXPECT_EQ(c.Impairments.AdcBits, 5);
    EXPECT_EQ(c.ImpairmentSeed, 77u);
    CommandLine wgn("test");
    const char *bad[] = {"siggen", "--modulation", "WGN", "--cfo-hz", "5"};
    wgn.App.parse(std::size(bad), bad);
    EXPECT_THROW(ResolveConfig(wgn), std::invalid_argument);
    CommandLine batch("test");
    const char *b[] = {"siggen", "batch", "--cfo-hz", "5", "--adc-bits", "8"};
    batch.App.parse(std::size(b), b);
    const auto r = ResolveBatch(batch);
    EXPECT_EQ(r.Base.Impairments.CfoHz, 5);
    EXPECT_EQ(r.Base.Impairments.AdcBits, 8);
}

TEST(CliConfig, FskOptionsResolveAndRejectInapplicableOnes) {
    CommandLine cli("test");
    const char *args[] = {"siggen", "--modulation", "4-fsk", "--tone-spacing-hz", "600", "--symbol-rate",
                          "1000",   "--sps",        "8"};
    cli.App.parse(std::size(args), args);
    const auto c = ResolveConfig(cli);
    EXPECT_EQ(c.Modulation, Core::Modulation::FSK4);
    EXPECT_EQ(c.ToneSpacingHz, 600);
    for (const auto &bad :
         std::vector<std::vector<const char *>>{{"siggen", "--modulation", "MSK", "--tone-spacing-hz", "600"},
                                                {"siggen", "--modulation", "QPSK", "--tone-spacing-hz", "600"},
                                                {"siggen", "--modulation", "2-FSK", "--pulse", "rrc"},
                                                {"siggen", "--modulation", "MSK", "--roll-off", "0.3"},
                                                {"siggen", "--modulation", "4-FSK", "--sps", "1"}}) {
        CommandLine rejected("test");
        rejected.App.parse(static_cast<int>(bad.size()), bad.data());
        EXPECT_THROW(ResolveConfig(rejected), std::invalid_argument);
    }
    CommandLine msk("test");
    const char *ok[] = {"siggen", "--modulation", "msk", "--snr-db", "12", "--cfo-hz", "3"};
    msk.App.parse(std::size(ok), ok);
    const auto m = ResolveConfig(msk);
    EXPECT_EQ(m.Modulation, Core::Modulation::MSK);
    EXPECT_TRUE(m.Awgn.Enabled);
    CommandLine batch("test");
    const char *mixed[] = {"siggen", "batch", "--modulations", "QPSK", "2-FSK", "--tone-spacing-hz", "800",
                           "--sps",  "8"};
    batch.App.parse(std::size(mixed), mixed);
    EXPECT_EQ(ResolveBatch(batch).Base.ToneSpacingHz, 800);
}

} // namespace Console
