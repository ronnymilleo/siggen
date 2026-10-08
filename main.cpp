/**
 * @file    main.cpp
 * @brief   Entry point of siggen: parses the command line, then generates, runs a batch, analyzes a recording,
 *          measures a BER curve or opens the GUI.
 */

#include "analysis.h"
#include "ber.h"
#include "cli_config.h"
#include "command_line.h"
#include "generator.h"
#include "iq_export.h"
#include "recording.h"
#include <iostream>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#if SIGGEN_HAS_GUI
#include "gui_runner.h"
#endif

/**
 * @brief   Parses the command line and runs the selected mode: batch, analyze, ber, GUI, or a single generation
 *          exported to file.
 * @param[in] argc  Argument count.
 * @param[in] argv  Arguments.
 * @return  0 on success; 1 on any error, or CLI11's exit code for --help, --version and parse errors.
 * @note    The log level comes from SPDLOG_LEVEL unless --log-level overrides it. Logs go to stderr.
 */
int main(int argc, char **argv) {
    try {
        Console::CommandLine command_line(SIGGEN_VERSION);
        try {
            command_line.App.parse(argc, argv);
        } catch (const CLI::ParseError &error) {
            return command_line.App.exit(error);
        }

        spdlog::set_default_logger(spdlog::stderr_color_mt("siggen"));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
        spdlog::cfg::load_env_levels();
        if (!command_line.LogLevel.empty()) {
            spdlog::set_level(spdlog::level::from_str(command_line.LogLevel));
        }
        spdlog::flush_on(spdlog::level::warn);

        if (command_line.BatchSelected()) {
            const auto request = Console::ResolveBatch(command_line);
            spdlog::info("Generating batch into {}", request.OutputDir.string());
            const auto summary = Core::RunBatch(request);
            spdlog::info("Batch complete: {} frames across {} sweep points", summary.FrameCount, summary.PointCount);
            return 0;
        }

        if (command_line.AnalyzeSelected()) {
            for (const auto *option : command_line.App.get_options()) {
                if (option->count() && option->get_name() != "--log-level") {
                    throw std::invalid_argument("With analyze, no signal or export options apply; root option: " +
                                                option->get_name());
                }
            }
            if (command_line.AnalyzeSegment < 4) {
                throw std::invalid_argument("--segment must be at least 4 samples");
            }
            const auto recording = Core::ReadRecording(command_line.AnalyzeFile);
            const auto report = Core::AnalyzeRecording(recording, Console::ResolveWindow(command_line.AnalyzeWindow),
                                                       static_cast<std::size_t>(command_line.AnalyzeSegment));
            std::cout << (command_line.AnalyzeJson ? Core::ReportJson(report) : Core::ReportText(report));
            return 0;
        }

        if (command_line.BerSelected()) {
            const auto request = Console::ResolveBer(command_line);
            const auto points = Core::BerSweep(request.Config, request.Settings);
            std::cout << (command_line.BerJson ? Core::BerJson(request.Config, points)
                                               : Core::BerText(request.Config, points));
            return 0;
        }

        const auto config = Console::ResolveConfig(command_line);

        if (command_line.Gui) {
#if SIGGEN_HAS_GUI
            spdlog::info("Starting Siggen GUI");
            return GUI::RunGUI(config);
#else
            spdlog::error(
                "GUI support is not included in this build. Build with the dev or release preset to use --gui.");
            return 1;
#endif
        }

        const auto format = Console::ResolveFormat(command_line.Format);
        const auto output = Console::ResolveOutput(command_line);
        spdlog::debug("Generating {} signal", Core::ModulationName(config.Modulation));
        const auto signal = Core::Generate(config);
        Core::ExportSignal(output, signal, format, command_line.Overwrite);
        spdlog::info("Exported {} complex samples to {} and its metadata file", signal.Samples.size(), output);
        return 0;
    } catch (const std::exception &error) {
        spdlog::error("Operation failed: {}", error.what());
        return 1;
    } catch (...) {
        spdlog::critical("Unknown error occurred");
        return 1;
    }
}
