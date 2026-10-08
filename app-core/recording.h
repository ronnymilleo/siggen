/**
 * @file    recording.h
 * @brief   Reads SigMF recordings back and rebuilds the generated signal they came from.
 */

#ifndef SIGGEN_RECORDING_H
#define SIGGEN_RECORDING_H

#include "generator.h"
#include <complex>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  Recording
 * @brief   I/Q samples read back from a file, with whatever the file says about them.
 */
struct Recording {
    /**
     * @struct  FrameSpan
     * @brief   Where a batch frame sits in the full guarded buffer it was cut from.
     * @details The frame occupies [CropOffset, CropOffset + Size) of the buffer that `Config` describes.
     */
    struct FrameSpan {
        std::size_t CropOffset = 0;
        std::size_t Size = 0;
    };

    std::vector<std::complex<float>> Samples;
    double SampleRateHz = 0;
    std::string Datatype; // SigMF datatype of the source (cf32_le or ci16_le)
    std::string Description;
    // Present when the file carries siggen's own configuration (`siggen:preset`). It describes how the samples
    // were generated, not what was done to them since
    std::optional<GenerationConfig> Config;
    // Set for batch frames: `Config` then describes the full guarded buffer the frame was cut from
    std::optional<FrameSpan> Frame;
};

/**
 * @struct  FrameScoring
 * @brief   A batch frame placed back into its full buffer, with the symbol range that can be scored.
 * @details Outside the frame the buffer holds the regenerated clean signal. The symbol range
 *          [FirstSymbol, EndSymbol) covers the symbols whose matched-filter windows lie wholly inside the frame,
 *          so only real frame samples are scored.
 */
struct FrameScoring {
    GeneratedSignal Signal;
    std::size_t FirstSymbol = 0;
    std::size_t EndSymbol = 0;
};

Recording ReadRecording(const std::filesystem::path &path);
std::optional<GeneratedSignal> SignalFromRecording(const Recording &recording);
std::optional<FrameScoring> ScoreFrame(const Recording &recording);

} // namespace Core

#endif // SIGGEN_RECORDING_H
