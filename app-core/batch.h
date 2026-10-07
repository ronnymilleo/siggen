/**
 * @file    batch.h
 * @brief   Generates sweeps of fixed-length frames over waveforms, seeds and SNRs into a dataset directory.
 */

#ifndef SIGGEN_BATCH_H
#define SIGGEN_BATCH_H

#include "generator.h"
#include "iq_export.h"
#include <complex>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Core {

inline constexpr std::size_t MaxBatchFrames = 100000;

/**
 * @struct  FrameResult
 * @brief   One generated frame and the provenance its sidecar and manifest record.
 */
struct FrameResult {
    std::vector<std::complex<float>> Samples; // Exactly frame_size samples
    double SampleRateHz = 0;
    std::size_t CropOffset = 0;         // Start of the frame in the full generated buffer
    std::size_t FilterDelaySamples = 0; // Original filter delay before cropping
    std::uint32_t DataSeed = 0;
    std::uint32_t NoiseSeed = 0;
    NoiseRecord Noise;               // AWGN provenance when applied
    bool ImpairmentsApplied = false; // Channel impairments run after AWGN on the cropped frame
    std::uint32_t ImpairmentSeed = 0;
};

/**
 * @struct  BatchRequest
 * @brief   A sweep: every waveform times every seed times every SNR, with the same number of frames per point.
 */
struct BatchRequest {
    GenerationConfig Base;             // Resolved shared configuration
    std::vector<Modulation> Waveforms; // Empty selects base.modulation
    std::vector<std::uint32_t> Seeds;  // Empty selects base.seed
    std::vector<double> SnrsDb;        // Empty retains the base noise setting
    int FrameSize = 2048;
    int FramesPerPoint = 1;
    ExportFormat Format = ExportFormat::BinaryFloat32;
    std::filesystem::path OutputDir;
};

/**
 * @struct  BatchSummary
 * @brief   Counts of a completed batch.
 */
struct BatchSummary {
    std::size_t PointCount = 0;
    std::size_t FrameCount = 0;
};

// Seed derivation
std::uint32_t DeriveDataSeed(std::uint32_t base_seed, Modulation waveform, std::size_t frame_index);
std::uint32_t DeriveNoiseSeed(std::uint32_t base_seed, Modulation waveform, std::size_t frame_index,
                              std::uint32_t configured_noise_seed);
std::uint32_t DeriveImpairmentSeed(std::uint32_t base_seed, Modulation waveform, std::size_t frame_index,
                                   std::uint32_t configured_impairment_seed);

// Frames
void ValidateFrameRequest(const GenerationConfig &base, Modulation waveform, int frame_size);
FrameResult GenerateFrame(const GenerationConfig &base, int frame_size, std::size_t frame_index,
                          const std::optional<double> &snr_db);

// Batches
void ValidateBatch(const BatchRequest &request);
BatchSummary RunBatch(const BatchRequest &request);

} // namespace Core

#endif // SIGGEN_BATCH_H
