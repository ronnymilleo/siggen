#pragma once
#include "generator.h"
#include "iq_export.h"
#include <complex>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace iq {
inline constexpr std::size_t MAX_BATCH_FRAMES = 100000;

// Deterministic per-frame seed derivation via std::seed_seq over
// {base seed, waveform id, frame index, stream tag} with fixed stream tags
// data = 1, noise = 2 and impairments = 3. Noise derivation additionally includes the
// configured noise seed. SNR is deliberately excluded so every SNR point of a
// (waveform, seed) sweep shares the same underlying data and noise draws.
std::uint32_t derive_data_seed(std::uint32_t base_seed, Modulation waveform, std::size_t frame_index);
std::uint32_t derive_noise_seed(std::uint32_t base_seed, Modulation waveform, std::size_t frame_index,
                                std::uint32_t configured_noise_seed);
std::uint32_t derive_impairment_seed(std::uint32_t base_seed, Modulation waveform, std::size_t frame_index,
                                     std::uint32_t configured_impairment_seed);

struct FrameResult {
    std::vector<std::complex<float>> samples; // Exactly frame_size samples.
    double sample_rate_hz = 0;
    std::size_t crop_offset = 0;              // Start of the frame in the full generated buffer.
    std::size_t filter_delay_samples = 0;     // Original filter delay before cropping.
    std::uint32_t data_seed = 0;
    std::uint32_t noise_seed = 0;
    NoiseRecord noise;                        // AWGN provenance when applied.
    bool impairments_applied = false;         // Channel impairments run after AWGN on the cropped frame.
    std::uint32_t impairment_seed = 0;
};
// Generate one fixed-length frame from base.modulation. A supplied snr_db
// enables AWGN at that value for linear waveforms; nullopt retains the base
// configuration's noise setting. Noise sources ignore SNR entirely.
// Frame timestamps always start at zero; the crop offset is reported separately.
FrameResult generate_frame(const GenerationConfig& base, int frame_size, std::size_t frame_index,
                           const std::optional<double>& snr_db);
// Checked frame arithmetic and limit validation without generating samples.
void validate_frame_request(const GenerationConfig& base, Modulation waveform, int frame_size);

struct BatchRequest {
    GenerationConfig base;               // Resolved shared configuration.
    std::vector<Modulation> waveforms;   // Empty selects base.modulation.
    std::vector<std::uint32_t> seeds;    // Empty selects base.seed.
    std::vector<double> snrs_db;         // Empty retains the base noise setting.
    int frame_size = 2048;
    int frames_per_point = 1;
    ExportFormat format = ExportFormat::SigMF;
    std::filesystem::path output_dir;
};
struct BatchSummary {
    std::size_t point_count = 0;
    std::size_t frame_count = 0;
};
// Validates the complete sweep and checked size arithmetic before creating any
// output. The output directory must not exist; existing directories are left
// untouched. Fails fast on generation/write errors, preserving completed
// frames; an interrupted run simply lacks its completion summary record.
void validate_batch(const BatchRequest& request);
BatchSummary run_batch(const BatchRequest& request);
}
