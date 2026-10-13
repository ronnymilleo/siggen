/**
 * @file    main.cpp
 * @brief   Entry point of the siggen command-line tool: parses the command line, then generates a single signal,
 *          runs a batch, analyzes a recording or measures a BER curve.
 */

#include "analysis.h"
#include "ber.h"
#include "cli_config.h"
#include "command_line.h"
#include "error_codes.h"
#include "generator.h"
#include "iq_export.h"
#include "recording.h"
#include <iostream>
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

/**
 * @brief   Parses the command line and runs the selected mode: batch, analyze, ber, or a single generation
 *          exported to file.
 * @param[in] argc  Argument count.
 * @param[in] argv  Arguments.
 * @return  Core::ExitSuccess on success; Core::ExitFailure on any error, or CLI11's exit code for --help,
 *          --version and parse errors.
 * @note    The log level comes from SPDLOG_LEVEL unless --log-level overrides it. Logs go to stderr. CLI11
 *          reports its parse errors with exceptions, caught at that vendor boundary only.
 */
int main(int argc, char **argv) {
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
        if (!request.has_value()) {
            spdlog::error("Operation failed: {}", request.error());
            return Core::ExitFailure;
        }
        spdlog::info("Generating batch into {}", request->OutputDir.string());
        const auto summary = Core::RunBatch(*request);
        if (!summary.has_value()) {
            spdlog::error("Operation failed: {}", summary.error());
            return Core::ExitFailure;
        }
        spdlog::info("Batch complete: {} frames across {} sweep points", summary->FrameCount, summary->PointCount);
        return Core::ExitSuccess;
    }

    if (command_line.AnalyzeSelected()) {
        for (const auto *option : command_line.App.get_options()) {
            if (option->count() && option->get_name() != "--log-level") {
                spdlog::error("Operation failed: With analyze, no signal or export options apply; root option: {}",
                              option->get_name());
                return Core::ExitFailure;
            }
        }
        if (command_line.AnalyzeSegment < 4) {
            spdlog::error("Operation failed: --segment must be at least 4 samples");
            return Core::ExitFailure;
        }
        const auto window = Console::ResolveWindow(command_line.AnalyzeWindow);
        if (!window.has_value()) {
            spdlog::error("Operation failed: {}", window.error());
            return Core::ExitFailure;
        }
        const auto recording = Core::ReadRecording(command_line.AnalyzeFile);
        if (!recording.has_value()) {
            spdlog::error("Operation failed: {}", recording.error());
            return Core::ExitFailure;
        }
        const auto report =
            Core::AnalyzeRecording(*recording, *window, static_cast<std::size_t>(command_line.AnalyzeSegment));
        if (!report.has_value()) {
            spdlog::error("Operation failed: {}", report.error());
            return Core::ExitFailure;
        }
        std::cout << (command_line.AnalyzeJson ? Core::ReportJson(*report) : Core::ReportText(*report));
        return Core::ExitSuccess;
    }

    if (command_line.BerSelected()) {
        const auto request = Console::ResolveBer(command_line);
        if (!request.has_value()) {
            spdlog::error("Operation failed: {}", request.error());
            return Core::ExitFailure;
        }
        const auto points = Core::BerSweep(request->Config, request->Settings);
        if (!points.has_value()) {
            spdlog::error("Operation failed: {}", points.error());
            return Core::ExitFailure;
        }
        std::cout << (command_line.BerJson ? Core::BerJson(request->Config, *points)
                                           : Core::BerText(request->Config, *points));
        return Core::ExitSuccess;
    }

    const auto config = Console::ResolveConfig(command_line);
    if (!config.has_value()) {
        spdlog::error("Operation failed: {}", config.error());
        return Core::ExitFailure;
    }
    const auto format = Console::ResolveFormat(command_line.Format);
    if (!format.has_value()) {
        spdlog::error("Operation failed: {}", format.error());
        return Core::ExitFailure;
    }
    const auto output = Console::ResolveOutput(command_line);
    if (!output.has_value()) {
        spdlog::error("Operation failed: {}", output.error());
        return Core::ExitFailure;
    }
    spdlog::debug("Generating {} signal", Core::ModulationName(config->Modulation));
    const auto signal = Core::Generate(*config);
    if (!signal.has_value()) {
        spdlog::error("Operation failed: {}", signal.error());
        return Core::ExitFailure;
    }
    const auto exported = Core::ExportSignal(*output, *signal, *format, command_line.Overwrite);
    if (!exported.has_value()) {
        spdlog::error("Operation failed: {}", exported.error());
        return Core::ExitFailure;
    }
    spdlog::info("Exported {} complex samples to {} and its metadata file", signal->Samples.size(), *output);
    return Core::ExitSuccess;
}
