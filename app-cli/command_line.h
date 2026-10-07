/**
 * @file    command_line.h
 * @brief   Command-line options of siggen, parsed with CLI11.
 */

#ifndef SIGGEN_COMMAND_LINE_H
#define SIGGEN_COMMAND_LINE_H

#include <CLI/CLI.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Console {

/**
 * @struct  CommandLine
 * @brief   The CLI11 parser and the raw values of every option, for single generation, the GUI and batch.
 * @details Parsing stays independent of graphics, logging and library resolution. Precedence is defaults ->
 *          loaded preset -> explicitly supplied options; the resolver inspects each option's count() on the parsed
 *          App or subcommand, so unspecified options never clobber preset values.
 */
struct CommandLine {
    explicit CommandLine(const std::string &version);

    bool BatchSelected() const;

    CLI::App App{"Siggen: complex baseband generation and analysis"};

    // Runtime controls
    std::string LogLevel;
    bool Gui = false;

    // Single-generation export controls (incompatible with --gui and batch)
    std::string Output; // Empty selects signal.csv / signal.iq by format
    bool Overwrite = false;
    std::string Format = "csv";

    // Shared preset and signal settings (allowed with --gui and batch)
    std::string Preset;
    std::string Modulation;
    int Symbols = 0;
    double SymbolRate = 0;
    int Sps = 0;
    std::string Pulse;
    double ToneSpacingHz = 0;
    double RollOff = 0;
    int Span = 0;
    double Gain = 0;
    std::uint32_t Seed = 0;
    std::string DataSource;
    std::string Bits;

    // Noise controls
    int Samples = 0;
    double SampleRate = 0;
    double NoisePower = 0;
    std::uint32_t NoiseSeed = 0;
    std::string SnrDb; // Numeric, or "off" to disable preset AWGN

    // Channel impairments (linear waveforms)
    double CfoHz = 0;
    double PhaseNoiseHz = 0;
    double IqGainDb = 0;
    double IqPhaseDeg = 0;
    double DcI = 0;
    double DcQ = 0;
    int AdcBits = 0;
    std::uint32_t ImpairmentSeed = 0;

    // Batch subcommand state
    CLI::App *Batch = nullptr;
    std::string BatchFormat = "cf32";
    std::vector<std::string> BatchModulations;
    std::vector<std::uint32_t> BatchSeeds;
    std::vector<double> BatchSnrsDb;
    int FrameSize = 2048;
    int FramesPerPoint = 1;
    std::string OutputDir = "dataset";
    bool BatchOverwrite = false;

private:
    void AddSignalOptions(CLI::App &target);
};

} // namespace Console

#endif // SIGGEN_COMMAND_LINE_H
