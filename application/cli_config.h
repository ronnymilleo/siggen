#pragma once
#include "batch.h"
#include "command_line.h"
#include "generator.h"
#include "iq_export.h"
#include "signal_analysis.h"
#include <string>

// Resolve configuration as defaults -> loaded preset -> explicitly supplied
// CLI options, then validate through the shared library. Options that were not
// supplied never replace preset values. Throws std::invalid_argument on
// unknown names or options incompatible with the selected waveform family.
iq::GenerationConfig resolve_config(const CommandLine& cli);
iq::ExportFormat resolve_format(const std::string& format);
// Explicit --output, else signal.csv or signal.sigmf-data by format.
std::string resolve_output(const CommandLine& cli);
// Batch request: shared preset/overrides plus validated sweep axes.
iq::BatchRequest resolve_batch(const CommandLine& cli);
iq::Window resolve_window(const std::string& name);
