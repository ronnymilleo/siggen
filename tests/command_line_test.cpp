#include "command_line.h"
#include <gtest/gtest.h>
#include <iterator>

TEST(CommandLine, NoArgumentsLeavesEnvironmentFallbackAvailable)
{
    CommandLine cli("test-version");
    const char* argv[] = {"siggen"};
    cli.app.parse(1, argv);
    EXPECT_TRUE(cli.log_level.empty());
}

TEST(CommandLine, AcceptsEverySupportedLogLevel)
{
    for (const char* level : {"trace", "debug", "info", "warn", "error", "critical", "off"})
    {
        CommandLine cli("test-version");
        const char* argv[] = {"siggen", "--log-level", level};
        cli.app.parse(3, argv);
        EXPECT_EQ(cli.log_level, level);
    }
}

TEST(CommandLine, AcceptsEqualsSyntax)
{
    CommandLine cli("test-version");
    const char* argv[] = {"siggen", "--log-level=debug"};
    cli.app.parse(2, argv);
    EXPECT_EQ(cli.log_level, "debug");
}

TEST(CommandLine, RejectsInvalidLogLevel)
{
    CommandLine cli("test-version");
    const char* argv[] = {"siggen", "--log-level", "verbose"};
    EXPECT_THROW(cli.app.parse(3, argv), CLI::ValidationError);
}

TEST(CommandLine, RejectsMissingLogLevel)
{
    CommandLine cli("test-version");
    const char* argv[] = {"siggen", "--log-level"};
    EXPECT_THROW(cli.app.parse(2, argv), CLI::ParseError);
}

TEST(CommandLine, RejectsUnknownOptions)
{
    CommandLine cli("test-version");
    const char* argv[] = {"siggen", "--unknown"};
    EXPECT_THROW(cli.app.parse(2, argv), CLI::ExtrasError);
}

TEST(CommandLine, HelpAndVersionExitParsing)
{
    CommandLine help("test-version");
    const char* help_argv[] = {"siggen", "--help"};
    EXPECT_THROW(help.app.parse(2, help_argv), CLI::CallForHelp);
    for (const char* expected : {"--log-level", "--modulation", "--preset", "--snr-db", "--noise-power", "batch"})
        EXPECT_NE(help.app.help().find(expected), std::string::npos) << expected;
    CommandLine version("test-version");
    const char* version_argv[] = {"siggen", "--version"};
    EXPECT_THROW(version.app.parse(2, version_argv), CLI::CallForVersion);
    EXPECT_EQ(version.app.version(), "Siggen test-version");
}

TEST(CommandLine, DefaultModeExportsCsv)
{
    CommandLine cli("test-version");
    const char* argv[] = {"siggen"};
    cli.app.parse(1, argv);
    EXPECT_FALSE(cli.gui);
    EXPECT_FALSE(cli.overwrite);
    EXPECT_TRUE(cli.output.empty());
    EXPECT_EQ(cli.format, "csv");
    EXPECT_FALSE(cli.batch_selected());
}

TEST(CommandLine, GuiCannotSilentlyIgnoreExportOptions)
{
    for (const char* option : {"--output=custom.csv", "--overwrite", "--format=sigmf"})
    {
        CommandLine cli("test-version");
        const char* argv[] = {"siggen", "--gui", option};
        EXPECT_THROW(cli.app.parse(3, argv), CLI::ExcludesError) << option;
    }
}

TEST(CommandLine, GuiAcceptsPresetAndSignalOptions)
{
    CommandLine cli("test-version");
    const char* argv[] = {"siggen", "--gui", "--preset", "custom.preset", "--modulation", "64-QAM",
                          "--gain", "2", "--snr-db", "10"};
    cli.app.parse(std::size(argv), argv);
    EXPECT_TRUE(cli.gui);
    EXPECT_EQ(cli.preset, "custom.preset");
    EXPECT_EQ(cli.modulation, "64-QAM");
    EXPECT_DOUBLE_EQ(cli.gain, 2);
    EXPECT_EQ(cli.snr_db, "10");
}

TEST(CommandLine, GuiAndBatchAreExclusive)
{
    CommandLine cli("test-version");
    const char* argv[] = {"siggen", "--gui", "batch"};
    EXPECT_THROW(cli.app.parse(3, argv), CLI::ExcludesError);
}

TEST(CommandLine, SignalAndNoiseOptionsParse)
{
    CommandLine cli("test-version");
    const char* argv[] = {"siggen", "--modulation", "8-PSK", "--symbols", "64", "--symbol-rate", "2000",
                          "--sps", "4", "--pulse", "rrc", "--roll-off", "0.35", "--span", "6",
                          "--gain", "1.5", "--seed", "7", "--noise-seed", "9", "--snr-db", "off",
                          "--format", "sigmf"};
    cli.app.parse(std::size(argv), argv);
    EXPECT_EQ(cli.modulation, "8-PSK");
    EXPECT_EQ(cli.symbols, 64);
    EXPECT_DOUBLE_EQ(cli.symbol_rate, 2000);
    EXPECT_EQ(cli.sps, 4);
    EXPECT_EQ(cli.pulse, "rrc");
    EXPECT_DOUBLE_EQ(cli.roll_off, 0.35);
    EXPECT_EQ(cli.span, 6);
    EXPECT_DOUBLE_EQ(cli.gain, 1.5);
    EXPECT_EQ(cli.seed, 7u);
    EXPECT_EQ(cli.noise_seed, 9u);
    EXPECT_EQ(cli.snr_db, "off");
    EXPECT_EQ(cli.format, "sigmf");
}

TEST(CommandLine, RejectsInvalidMembers)
{
    CommandLine cli("test-version");
    const char* bad_pulse[] = {"siggen", "--pulse", "gaussian"};
    EXPECT_THROW(cli.app.parse(3, bad_pulse), CLI::ValidationError);
    CommandLine source_cli("test-version");
    const char* bad_source[] = {"siggen", "--data-source", "file"};
    EXPECT_THROW(source_cli.app.parse(3, bad_source), CLI::ValidationError);
    CommandLine format_cli("test-version");
    const char* bad_format[] = {"siggen", "--format", "mat"};
    EXPECT_THROW(format_cli.app.parse(3, bad_format), CLI::ValidationError);
}

TEST(CommandLine, BatchSubcommandParsesAxes)
{
    CommandLine cli("test-version");
    const char* argv[] = {"siggen", "batch", "--modulations", "BPSK", "QPSK", "8-PSK", "16-QAM", "64-QAM", "WGN",
                          "--seeds", "42", "43", "--snrs-db=-10,0,10", "--frame-size", "128",
                          "--frames-per-point", "3", "--output-dir", "out", "--format", "csv", "--gain", "2"};
    cli.app.parse(std::size(argv), argv);
    ASSERT_TRUE(cli.batch_selected());
    EXPECT_EQ(cli.batch_modulations, (std::vector<std::string>{"BPSK", "QPSK", "8-PSK", "16-QAM", "64-QAM", "WGN"}));
    EXPECT_EQ(cli.batch_seeds, (std::vector<std::uint32_t>{42, 43}));
    EXPECT_EQ(cli.batch_snrs_db, (std::vector<double>{-10, 0, 10}));
    EXPECT_EQ(cli.frame_size, 128);
    EXPECT_EQ(cli.frames_per_point, 3);
    EXPECT_EQ(cli.output_dir, "out");
    EXPECT_EQ(cli.batch_format, "csv");
    EXPECT_DOUBLE_EQ(cli.gain, 2);
    EXPECT_FALSE(cli.batch_overwrite);
}

TEST(CommandLine, BatchDefaults)
{
    CommandLine cli("test-version");
    const char* argv[] = {"siggen", "batch"};
    cli.app.parse(2, argv);
    ASSERT_TRUE(cli.batch_selected());
    EXPECT_TRUE(cli.batch_modulations.empty());
    EXPECT_TRUE(cli.batch_seeds.empty());
    EXPECT_TRUE(cli.batch_snrs_db.empty());
    EXPECT_EQ(cli.frame_size, 2048);
    EXPECT_EQ(cli.frames_per_point, 1);
    EXPECT_EQ(cli.output_dir, "dataset");
    EXPECT_EQ(cli.batch_format, "sigmf");
}

TEST(CommandLine, BatchOverwriteFlagIsDetected)
{
    CommandLine cli("test-version");
    const char* argv[] = {"siggen", "batch", "--overwrite"};
    cli.app.parse(3, argv);
    EXPECT_TRUE(cli.batch_overwrite);
}
