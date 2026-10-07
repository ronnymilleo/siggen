#pragma once
#include "generator.h"
#include <complex>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace iq {
// I/Q samples read back from a file, with whatever the file says about them.
struct Recording {
    std::vector<std::complex<float>> samples;
    double sample_rate_hz = 0;
    std::string datatype;     // SigMF datatype of the source (cf32_le or ci16_le).
    std::string description;
    // Present when the file carries siggen's own configuration (`siggen:preset`).
    // It describes how the samples were generated, not what was done to them since.
    std::optional<GenerationConfig> config;
};
// Reads a SigMF recording given its `.sigmf-meta` or `.sigmf-data` path, or a
// siggen batch `cf32` frame given its sample file (metadata from `<path>.json`).
// Supports cf32_le and ci16_le (scaled by 1/32768). Throws std::runtime_error
// or std::invalid_argument with a descriptive message otherwise.
Recording read_recording(const std::filesystem::path& path);
// Rebuilds a GeneratedSignal around the recording's samples using its embedded
// configuration (symbols and bits are regenerated; the samples are the file's).
// Empty when the file has no configuration or its length does not match it.
std::optional<GeneratedSignal> signal_from_recording(const Recording& recording);
}
