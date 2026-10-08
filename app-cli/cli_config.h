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

Core::GenerationConfig ResolveConfig(const CommandLine &cli);
Core::ExportFormat ResolveFormat(const std::string &format);
std::string ResolveOutput(const CommandLine &cli);
Core::BatchRequest ResolveBatch(const CommandLine &cli);
BerRequest ResolveBer(const CommandLine &cli);
Core::Window ResolveWindow(const std::string &name);

} // namespace Console

#endif // SIGGEN_CLI_CONFIG_H
