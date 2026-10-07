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
    // Set for batch frames: `config` then describes the full guarded buffer the frame was cut from,
    // and the frame occupies [crop_offset, crop_offset + size) of it.
    struct Frame { std::size_t crop_offset = 0; std::size_t size = 0; };
    std::optional<Frame> frame;
};
// Reads a SigMF recording given its `.sigmf-meta` or `.sigmf-data` path, or a
// legacy siggen `cf32` export given its sample file (metadata from `<path>.json`).
// Supports cf32_le and ci16_le (scaled by 1/32768). Throws std::runtime_error
// or std::invalid_argument with a descriptive message otherwise.
Recording read_recording(const std::filesystem::path& path);
// Rebuilds a GeneratedSignal around the recording's samples using its embedded
// configuration (symbols and bits are regenerated; the samples are the file's).
// Empty when the file has no configuration, is a batch frame, or its length does not match.
std::optional<GeneratedSignal> signal_from_recording(const Recording& recording);

// A batch frame placed back into its full buffer (the regenerated clean signal outside the frame),
// with the symbol range [first_symbol, end_symbol) whose matched-filter windows lie wholly inside
// the frame, so only real frame samples are scored. Empty unless the file is a linear batch frame
// that carries its generator preset.
struct FrameScoring {
    GeneratedSignal signal;
    std::size_t first_symbol = 0;
    std::size_t end_symbol = 0;
};
std::optional<FrameScoring> frame_scoring(const Recording& recording);
}
