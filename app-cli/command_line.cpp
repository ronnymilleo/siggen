/**
 * @file    command_line.cpp
 * @brief   Command-line options of siggen, parsed with CLI11.
 */

#include "command_line.h"

namespace Console {

/**
 * @brief   Declares every option, check and exclusion of the root command and of the batch, analyze and ber
 *          subcommands.
 * @param[in] version  Version printed by --version, after "Siggen ".
 */
CommandLine::CommandLine(const std::string &version) {
    auto *gui_option = App.add_flag("--gui", Gui, "Open the GUI, optionally initialized by preset/signal options");

    App.add_option("-o,--output", Output, "Sample destination (default: signal.csv or signal.sigmf-data)")
        ->excludes(gui_option);
    App.add_flag("--overwrite", Overwrite, "Replace existing sample and metadata files")->excludes(gui_option);
    App.add_option("--format", Format, "Single-generation output format")
        ->check(CLI::IsMember({"csv", "sigmf"}))
        ->excludes(gui_option);

    App.add_option("--preset", Preset, "Load a preset before applying explicit options");
    AddSignalOptions(App);

    App.set_version_flag("--version", "Siggen " + version);
    App.add_option("--log-level", LogLevel, "Console log level (overrides spdLOG_LEVEL; default: info)")
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error", "critical", "off"}));

    Batch = App.add_subcommand("batch", "Generate a swept dataset of fixed-length frames");
    Batch->add_option("--modulations", BatchModulations, "Waveform list to sweep (default: resolved waveform)")
        ->expected(-1);
    Batch->add_option("--seeds", BatchSeeds, "Seed list to sweep (default: resolved seed)")->expected(-1);
    Batch->add_option("--snrs-db", BatchSnrsDb, "SNR list, e.g. --snrs-db=-10,0,10")->delimiter(',')->expected(-1);
    Batch->add_option("--frame-size", FrameSize, "Samples per frame (default: 2048)");
    Batch->add_option("--frames-per-point", FramesPerPoint, "Frames per sweep point (default: 1)");
    Batch->add_option("--output-dir", OutputDir, "New output directory (must not exist; default: dataset)");
    Batch->add_option("--format", BatchFormat, "Frame format (default: sigmf)")->check(CLI::IsMember({"csv", "sigmf"}));
    Batch->add_flag("--overwrite", BatchOverwrite, "Rejected; batches never replace an existing directory");
    Batch->add_option("--preset", Preset, "Load a preset before applying explicit batch overrides");
    AddSignalOptions(*Batch);
    Batch->excludes(gui_option);

    Analyze = App.add_subcommand(
        "analyze", "Measure a SigMF recording (single signal or batch frame) (power, PAPR, spectrum, EVM)");
    Analyze->add_option("file", AnalyzeFile, "SigMF .sigmf-meta or .sigmf-data")->required();
    Analyze->add_option("--window", AnalyzeWindow, "Welch window (default: hann)")
        ->check(CLI::IsMember({"hann", "hamming", "blackman", "rectangular"}));
    Analyze->add_option("--segment", AnalyzeSegment, "Welch segment length in samples (default: 1024)");
    Analyze->add_flag("--json", AnalyzeJson, "Print the report as JSON");
    Analyze->excludes(gui_option);

    Ber = App.add_subcommand("ber", "Measure bit error rate against Eb/N0 with the ideal reference receiver");
    Ber->add_option("--eb-n0-db", BerEbN0Db,
                    "Eb/N0 points in dB, e.g. --eb-n0-db=0,2,4,6 (default: 0 to 10 in steps of 2)")
        ->delimiter(',')
        ->expected(-1);
    Ber->add_option("--min-errors", BerMinErrors, "Stop a point after this many bit errors (default: 100)");
    Ber->add_option("--max-bits", BerMaxBits, "Stop a point after this many bits (default: 2000000)");
    Ber->add_option("--block-symbols", BerBlockSymbols, "Symbols generated per block (default: 4096)");
    Ber->add_flag("--json", BerJson, "Print the curve as JSON");
    Ber->add_option("--preset", Preset, "Load a preset before applying explicit options");
    AddSignalOptions(*Ber);
    Ber->excludes(gui_option);
}

/**
 * @brief   Tells whether the batch subcommand was given.
 * @return  True after parsing a command line that contains "batch".
 */
bool CommandLine::BatchSelected() const {
    return Batch != nullptr && Batch->parsed();
}

/**
 * @brief   Tells whether the analyze subcommand was given.
 * @return  True after parsing a command line that contains "analyze".
 */
bool CommandLine::AnalyzeSelected() const {
    return Analyze != nullptr && Analyze->parsed();
}

/**
 * @brief   Tells whether the ber subcommand was given.
 * @return  True after parsing a command line that contains "ber".
 */
bool CommandLine::BerSelected() const {
    return Ber != nullptr && Ber->parsed();
}

/**
 * @brief   Adds the signal and noise options shared by single generation and the batch sweep.
 * @param[in,out] target  The root App or the batch subcommand.
 * @note    Both contexts bind the same members; only the context that is parsed assigns them.
 */
void CommandLine::AddSignalOptions(CLI::App &target) {
    target.add_option("--modulation", Modulation,
                      "Waveform: BPSK, QPSK, 8-PSK, 16-QAM, 32-QAM, 64-QAM, 256-QAM, OOK, 4-PAM, DBPSK, DQPSK, "
                      "pi/4-DQPSK, 8-DPSK, OQPSK, 4-ASK, 2-FSK, 4-FSK, MSK, WGN");
    target.add_option("--symbols", Symbols, "Symbol count (linear waveforms)");
    target.add_option("--symbol-rate", SymbolRate, "Symbol rate in baud (linear waveforms)");
    target.add_option("--sps", Sps, "Samples per symbol (linear waveforms)");
    target.add_option("--pulse", Pulse, "Pulse shape")->check(CLI::IsMember({"rrc", "rectangular"}));
    target.add_option("--tone-spacing-hz", ToneSpacingHz,
                      "FSK tone spacing in Hz (2-FSK/4-FSK; default 1000, MSK uses symbol rate / 2)");
    target.add_option("--roll-off", RollOff, "RRC roll-off in [0,1]");
    target.add_option("--span", Span, "RRC span in symbols");
    target.add_option("--gain", Gain, "Amplitude gain in [0,1000000]");
    target.add_option("--seed", Seed, "Data PRNG seed");
    target.add_option("--data-source", DataSource, "Data source")->check(CLI::IsMember({"random", "explicit"}));
    target.add_option("--bits", Bits, "Explicit binary digits (selects explicit input)");
    target.add_option("--samples", Samples, "WGN sample count");
    target.add_option("--sample-rate", SampleRate, "WGN sample rate in Hz");
    target.add_option("--noise-power", NoisePower, "WGN total complex noise power before gain");
    target.add_option("--noise-seed", NoiseSeed, "Noise PRNG seed (WGN source and AWGN)");
    target.add_option("--snr-db", SnrDb, "AWGN SNR in dB, or 'off' to disable preset AWGN");
    target.add_option("--cfo-hz", CfoHz, "Carrier frequency offset in Hz (linear waveforms)");
    target.add_option("--phase-noise-hz", PhaseNoiseHz,
                      "Oscillator phase-noise 3 dB linewidth in Hz (linear waveforms)");
    target.add_option("--iq-gain-db", IqGainDb, "IQ gain imbalance: Q gain relative to I, in dB (linear waveforms)");
    target.add_option("--iq-phase-deg", IqPhaseDeg, "IQ quadrature skew in degrees (linear waveforms)");
    target.add_option("--dc-i", DcI, "DC offset on I as a fraction of the RMS amplitude (linear waveforms)");
    target.add_option("--dc-q", DcQ, "DC offset on Q as a fraction of the RMS amplitude (linear waveforms)");
    target.add_option("--adc-bits", AdcBits, "Quantizer bits per component, 2-24; 0 disables (linear waveforms)");
    target.add_option("--impairment-seed", ImpairmentSeed, "Phase-noise PRNG seed");
}

} // namespace Console
