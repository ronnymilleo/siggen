/**
 * @file    cli_config.h
 * @brief   Turns parsed command-line options into a validated generation config or batch request.
 */

#ifndef SIGGEN_CLI_CONFIG_H
#define SIGGEN_CLI_CONFIG_H

#include "batch.h"
#include "command_line.h"
#include "generator.h"
#include "iq_export.h"
#include <string>

namespace Console {

Core::GenerationConfig ResolveConfig(const CommandLine &cli);
Core::ExportFormat ResolveFormat(const std::string &format);
std::string ResolveOutput(const CommandLine &cli);
Core::BatchRequest ResolveBatch(const CommandLine &cli);

} // namespace Console

#endif // SIGGEN_CLI_CONFIG_H
