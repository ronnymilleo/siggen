#pragma once

#include <CLI/CLI.hpp>
#include <cstdint>
#include <string>
#include <vector>

// Keep argument parsing independent of graphics, logging and library resolution.
// Precedence is defaults -> loaded preset -> explicitly supplied options; the
// resolver inspects each option's count() on the parsed App or subcommand so
// unspecified options never clobber preset values.
struct CommandLine
{
    CLI::App app{"Siggen: complex baseband generation and analysis"};

    // Runtime controls.
    std::string log_level;
    bool        gui = false;

    // Single-generation export controls (incompatible with --gui and batch).
    std::string output; // Empty selects signal.csv / signal.iq by format.
    bool        overwrite = false;
    std::string format = "csv";

    // Shared preset and signal settings (allowed with --gui and batch).
    std::string   preset;
    std::string   modulation;
    int           symbols = 0;
    double        symbol_rate = 0;
    int           sps = 0;
    std::string   pulse;
    double        roll_off = 0;
    int           span = 0;
    double        gain = 0;
    std::uint32_t seed = 0;
    std::string   data_source;
    std::string   bits;

    // Noise controls.
    int           samples = 0;
    double        sample_rate = 0;
    double        noise_power = 0;
    std::uint32_t noise_seed = 0;
    std::string   snr_db; // Numeric, or "off" to disable preset AWGN.

    // Channel impairments (linear waveforms).
    double        cfo_hz = 0;
    double        phase_noise_hz = 0;
    double        iq_gain_db = 0;
    double        iq_phase_deg = 0;
    double        dc_i = 0;
    double        dc_q = 0;
    int           adc_bits = 0;
    std::uint32_t impairment_seed = 0;

    // Batch subcommand state.
    CLI::App*                batch = nullptr;
    std::string              batch_format = "cf32";
    std::vector<std::string>   batch_modulations;
    std::vector<std::uint32_t> batch_seeds;
    std::vector<double>        batch_snrs_db;
    int                      frame_size = 2048;
    int                      frames_per_point = 1;
    std::string              output_dir = "dataset";
    bool                     batch_overwrite = false;

    explicit CommandLine(const std::string& version)
    {
        auto* gui_option = app.add_flag("--gui", gui, "Open the GUI, optionally initialized by preset/signal options");

        app.add_option("-o,--output", output, "Sample destination (default: signal.csv or signal.iq)")
            ->excludes(gui_option);
        app.add_flag("--overwrite", overwrite, "Replace existing sample and metadata files")->excludes(gui_option);
        app.add_option("--format", format, "Single-generation output format")
            ->check(CLI::IsMember({"csv", "cf32"}))
            ->excludes(gui_option);

        app.add_option("--preset", preset, "Load a preset before applying explicit options");
        add_signal_options(app);

        app.set_version_flag("--version", "Siggen " + version);
        app.add_option("--log-level", log_level,
                       "Console log level (overrides spdLOG_LEVEL; default: info)")
            ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error", "critical", "off"}));

        batch = app.add_subcommand("batch", "Generate a swept dataset of fixed-length frames");
        batch->add_option("--modulations", batch_modulations, "Waveform list to sweep (default: resolved waveform)")
            ->expected(-1);
        batch->add_option("--seeds", batch_seeds, "Seed list to sweep (default: resolved seed)")->expected(-1);
        batch->add_option("--snrs-db", batch_snrs_db, "SNR list, e.g. --snrs-db=-10,0,10")
            ->delimiter(',')
            ->expected(-1);
        batch->add_option("--frame-size", frame_size, "Samples per frame (default: 2048)");
        batch->add_option("--frames-per-point", frames_per_point, "Frames per sweep point (default: 1)");
        batch->add_option("--output-dir", output_dir, "New output directory (must not exist; default: dataset)");
        batch->add_option("--format", batch_format, "Frame format (default: cf32)")
            ->check(CLI::IsMember({"csv", "cf32"}));
        batch->add_flag("--overwrite", batch_overwrite, "Rejected; batches never replace an existing directory");
        batch->add_option("--preset", preset, "Load a preset before applying explicit batch overrides");
        add_signal_options(*batch);
        batch->excludes(gui_option);
    }

    bool batch_selected() const { return batch != nullptr && batch->parsed(); }

private:
    // Signal and noise options shared by single generation and the batch sweep.
    // Both bind the same members; only the parsed context assigns them.
    void add_signal_options(CLI::App& target)
    {
        target.add_option("--modulation", modulation, "Waveform: BPSK, QPSK, 8-PSK, 16-QAM, 64-QAM, WGN");
        target.add_option("--symbols", symbols, "Symbol count (linear waveforms)");
        target.add_option("--symbol-rate", symbol_rate, "Symbol rate in baud (linear waveforms)");
        target.add_option("--sps", sps, "Samples per symbol (linear waveforms)");
        target.add_option("--pulse", pulse, "Pulse shape")
            ->check(CLI::IsMember({"rrc", "rectangular"}));
        target.add_option("--roll-off", roll_off, "RRC roll-off in [0,1]");
        target.add_option("--span", span, "RRC span in symbols");
        target.add_option("--gain", gain, "Amplitude gain in [0,1000000]");
        target.add_option("--seed", seed, "Data PRNG seed");
        target.add_option("--data-source", data_source, "Data source")
            ->check(CLI::IsMember({"random", "explicit"}));
        target.add_option("--bits", bits, "Explicit binary digits (selects explicit input)");
        target.add_option("--samples", samples, "WGN sample count");
        target.add_option("--sample-rate", sample_rate, "WGN sample rate in Hz");
        target.add_option("--noise-power", noise_power, "WGN total complex noise power before gain");
        target.add_option("--noise-seed", noise_seed, "Noise PRNG seed (WGN source and AWGN)");
        target.add_option("--snr-db", snr_db, "AWGN SNR in dB, or 'off' to disable preset AWGN");
        target.add_option("--cfo-hz", cfo_hz, "Carrier frequency offset in Hz (linear waveforms)");
        target.add_option("--phase-noise-hz", phase_noise_hz, "Oscillator phase-noise 3 dB linewidth in Hz (linear waveforms)");
        target.add_option("--iq-gain-db", iq_gain_db, "IQ gain imbalance: Q gain relative to I, in dB (linear waveforms)");
        target.add_option("--iq-phase-deg", iq_phase_deg, "IQ quadrature skew in degrees (linear waveforms)");
        target.add_option("--dc-i", dc_i, "DC offset on I as a fraction of the RMS amplitude (linear waveforms)");
        target.add_option("--dc-q", dc_q, "DC offset on Q as a fraction of the RMS amplitude (linear waveforms)");
        target.add_option("--adc-bits", adc_bits, "Quantizer bits per component, 2-24; 0 disables (linear waveforms)");
        target.add_option("--impairment-seed", impairment_seed, "Phase-noise PRNG seed");
    }
};
