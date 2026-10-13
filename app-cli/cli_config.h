/**
 * @file    cli_config.h
 * @brief   Turns parsed command-line options into a validated generation config, batch request or BER request.
 */

#ifndef SIGGEN_CLI_CONFIG_H
#define SIGGEN_CLI_CONFIG_H

#include "batch.h"
#include "ber.h"
#include "command_line.h"
#include "generator.h"
#include "iq_export.h"
#include "signal_analysis.h"
#include <expected>
#include <string>

namespace Console {

/**
 * @struct  BerRequest
 * @brief   A BER sweep: the base configuration and the Eb/N0 points and stopping rules.
 */
struct BerRequest {
    Core::GenerationConfig Config;
    Core::BerSweepSettings Settings;
};

std::expected<Core::GenerationConfig, std::string> ResolveConfig(const CommandLine &cli);
std::expected<Core::ExportFormat, std::string> ResolveFormat(const std::string &format);
std::expected<std::string, std::string> ResolveOutput(const CommandLine &cli);
std::expected<Core::BatchRequest, std::string> ResolveBatch(const CommandLine &cli);
std::expected<BerRequest, std::string> ResolveBer(const CommandLine &cli);
std::expected<Core::Window, std::string> ResolveWindow(const std::string &name);

} // namespace Console

#endif // SIGGEN_CLI_CONFIG_H
