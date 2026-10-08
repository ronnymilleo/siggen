/**
 * @file    batch_test.cpp
 * @brief   Tests for batch frame generation, seed derivation and dataset output.
 */

#include "batch.h"

#include "analysis.h"
#include "iq_export.h"
#include "noise.h"
#include "recording.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#ifdef __linux__
#include <csignal>
#include <cstdlib>
#include <sys/resource.h>
#endif

namespace Core {

namespace {

std::filesystem::path UniqueDir(const char *prefix) {
    return std::filesystem::temp_directory_path() /
           (std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}
std::string ReadFile(const std::filesystem::path &path) {
    std::ifstream file(path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
std::vector<std::string> ReadLines(const std::filesystem::path &path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

} // namespace

TEST(Batch, SeedDerivationDeterministicAndSnrIndependent) {
    const auto data = DeriveDataSeed(42, Modulation::BPSK, 3);
    EXPECT_EQ(data, DeriveDataSeed(42, Modulation::BPSK, 3));
    EXPECT_NE(data, DeriveDataSeed(42, Modulation::BPSK, 4));
    EXPECT_NE(data, DeriveDataSeed(43, Modulation::BPSK, 3));
    EXPECT_NE(data, DeriveDataSeed(42, Modulation::QPSK, 3));
    EXPECT_NE(data, DeriveNoiseSeed(42, Modulation::BPSK, 3, 5490));
    // The configured noise seed participates in noise derivation only.
    EXPECT_EQ(DeriveNoiseSeed(42, Modulation::BPSK, 3, 5490), DeriveNoiseSeed(42, Modulation::BPSK, 3, 5490));
    EXPECT_NE(DeriveNoiseSeed(42, Modulation::BPSK, 3, 5490), DeriveNoiseSeed(42, Modulation::BPSK, 3, 5491));
    EXPECT_EQ(DeriveDataSeed(42, Modulation::BPSK, 3), DeriveDataSeed(42, Modulation::BPSK, 3));
}

TEST(Batch, FrameGeometryRRCAndRectangular) {
    GenerationConfig base;
    const int frame_size = 256;
    const auto rrc = GenerateFrame(base, frame_size, 0, std::nullopt);
    EXPECT_EQ(rrc.Samples.size(), 256u);
    EXPECT_EQ(rrc.FilterDelaySamples, 40u);
    EXPECT_EQ(rrc.CropOffset, 10u * 8u + 40u); // span*SPS + filter delay.
    EXPECT_FALSE(rrc.Noise.AwgnApplied);
    // The frame equals the middle of the guarded full generation with the derived seed.
    auto full_config = base;
    full_config.Seed = rrc.DataSeed;
    full_config.SymbolCount = 256 / 8 + 2 * 10; // N + 2G
    const auto full = Generate(full_config);
    ASSERT_GE(full.Samples.size(), rrc.CropOffset + 256u);
    for (std::size_t k = 0; k < 256; ++k) {
        EXPECT_EQ(rrc.Samples[k], full.Samples[rrc.CropOffset + k]);
    }

    base.Pulse = Pulse::Rectangular;
    const auto rect = GenerateFrame(base, frame_size, 0, std::nullopt);
    EXPECT_EQ(rect.Samples.size(), 256u);
    EXPECT_EQ(rect.CropOffset, 0u);
    EXPECT_EQ(rect.FilterDelaySamples, 0u);
    // Frame size not divisible by SPS still yields exact length.
    const auto odd = GenerateFrame(base, 100, 0, std::nullopt);
    EXPECT_EQ(odd.Samples.size(), 100u);
}

TEST(Batch, WGNFramesIgnoreSnrAxis) {
    GenerationConfig base;
    base.Modulation = Modulation::WGN;
    base.NoiseSource.SampleCount = 999; // Overridden by the frame size.
    const auto frame = GenerateFrame(base, 128, 0, std::optional<double>(10));
    EXPECT_EQ(frame.Samples.size(), 128u);
    EXPECT_FALSE(frame.Noise.AwgnApplied);
    EXPECT_EQ(frame.CropOffset, 0u);
    EXPECT_DOUBLE_EQ(frame.SampleRateHz, base.NoiseSource.SampleRateHz);
    // The frame is exactly the seeded WGN stream at configured power.
    const auto expected = GaussianNoise(128, base.NoiseSource.NoisePower, frame.NoiseSeed);
    EXPECT_EQ(frame.Samples, expected);
}

TEST(Batch, FrameAwgnAppliesAfterCrop) {
    GenerationConfig base;
    base.SymbolCount = 512; // Ignored; frame size fixes length.
    const auto clean = GenerateFrame(base, 256, 0, std::nullopt);
    const auto noisy = GenerateFrame(base, 256, 0, std::optional<double>(10));
    EXPECT_EQ(clean.DataSeed, noisy.DataSeed);
    EXPECT_EQ(clean.NoiseSeed, noisy.NoiseSeed);
    EXPECT_TRUE(noisy.Noise.AwgnApplied);
    EXPECT_EQ(noisy.Noise.ReferenceBegin, 0u);
    EXPECT_EQ(noisy.Noise.ReferenceEnd, 256u);
    double reference = 0;
    for (const auto &sample : clean.Samples) {
        reference += std::norm(sample);
    }
    reference /= 256;
    EXPECT_NEAR(noisy.Noise.ReferencePower, reference, 1e-9);
    const auto noise = GaussianNoise(256, noisy.Noise.AddedNoisePower, noisy.NoiseSeed);
    for (std::size_t k = 0; k < 256; ++k) {
        EXPECT_FLOAT_EQ((clean.Samples[k] + noise[k]).real(), noisy.Samples[k].real());
    }
    // SNR does not enter seed derivation: realizations stay paired.
    const auto louder = GenerateFrame(base, 256, 0, std::optional<double>(20));
    EXPECT_EQ(louder.DataSeed, noisy.DataSeed);
    EXPECT_EQ(louder.NoiseSeed, noisy.NoiseSeed);
}

TEST(Batch, FrameLimitsAndInvalidRequests) {
    GenerationConfig base;
    EXPECT_THROW(GenerateFrame(base, 0, 0, std::nullopt), std::invalid_argument);
    EXPECT_THROW(ValidateFrameRequest(base, static_cast<Modulation>(99), 64), std::invalid_argument);
    // Rectangular SPS=1 frames are limited by the 65536-symbol payload bound.
    base.Pulse = Pulse::Rectangular;
    base.SamplesPerSymbol = 1;
    EXPECT_THROW(ValidateFrameRequest(base, Modulation::BPSK, 65537), std::length_error);
    EXPECT_NO_THROW(ValidateFrameRequest(base, Modulation::BPSK, 65536));
    // RRC guards count toward the symbol limit.
    base = {};
    base.SamplesPerSymbol = 2;
    base.SpanSymbols = 100;
    EXPECT_THROW(ValidateFrameRequest(base, Modulation::BPSK, 200000), std::length_error);
}

TEST(Batch, RunWritesFramesSidecarsAndManifest) {
    const auto dir = UniqueDir("iq-batch-run-");
    BatchRequest request;
    request.Waveforms = {Modulation::BPSK, Modulation::WGN};
    request.Seeds = {42};
    request.SnrsDb = {0, 10};
    request.FrameSize = 64;
    request.FramesPerPoint = 2;
    request.OutputDir = dir;
    const auto summary = RunBatch(request);
    // BPSK has two SNR points, WGN ignores the SNR axis: three points, six frames.
    EXPECT_EQ(summary.PointCount, 3u);
    EXPECT_EQ(summary.FrameCount, 6u);
    const auto manifest = ReadLines(dir / "manifest.jsonl");
    ASSERT_EQ(manifest.size(), 8u); // header + six frames + summary
    EXPECT_NE(manifest.front().find("\"kind\":\"batch_header\""), std::string::npos);
    EXPECT_NE(manifest.front().find("\"manifest_version\":1"), std::string::npos);
    EXPECT_NE(manifest.front().find("\"waveforms\":[\"BPSK\",\"WGN\"]"), std::string::npos);
    EXPECT_NE(manifest.back().find("\"kind\":\"summary\""), std::string::npos);
    EXPECT_NE(manifest.back().find("\"completed\":true"), std::string::npos);
    EXPECT_NE(manifest.back().find("\"frame_count\":6"), std::string::npos);
    // User list order is preserved: BPSK points first, then WGN with a null SNR.
    for (std::size_t k = 1; k <= 4; ++k) {
        EXPECT_NE(manifest[k].find("\"waveform\":\"BPSK\""), std::string::npos) << k;
    }
    for (std::size_t k = 5; k <= 6; ++k) {
        EXPECT_NE(manifest[k].find("\"waveform\":\"WGN\""), std::string::npos) << k;
    }
    EXPECT_NE(manifest[5].find("\"snr_db\":null"), std::string::npos);
    EXPECT_NE(manifest[1].find("\"snr_db\":0"), std::string::npos);
    EXPECT_NE(manifest[3].find("\"snr_db\":10"), std::string::npos);
    // Deterministic indexed filenames, binary default, exact byte size.
    for (std::size_t point = 0; point < 3; ++point) {
        for (std::size_t frame = 0; frame < 2; ++frame) {
            std::ostringstream name;
            name << "frame_" << std::string(6 - std::to_string(point).size(), '0') << point << '_'
                 << std::string(6 - std::to_string(frame).size(), '0') << frame << ".sigmf-data";
            const auto data = dir / name.str();
            ASSERT_TRUE(std::filesystem::exists(data)) << name.str();
            EXPECT_EQ(std::filesystem::file_size(data), 64u * 8u);
            const auto sidecar = ReadFile(MetadataPath(dir / name.str()));
            EXPECT_NE(sidecar.find("\"kind\": \"batch_frame\""), std::string::npos);
            EXPECT_NE(sidecar.find("\"version\": 2"), std::string::npos);
            EXPECT_NE(sidecar.find("\"frame_start_s\": 0"), std::string::npos);
            if (point < 2) {
                EXPECT_NE(sidecar.find("\"crop\""), std::string::npos);
                EXPECT_NE(sidecar.find("\"offset_samples\": 120"), std::string::npos);
                EXPECT_NE(sidecar.find("\"filter_delay_samples\": 40"), std::string::npos);
            } else {
                EXPECT_NE(sidecar.find("\"noise_source\""), std::string::npos);
                EXPECT_EQ(sidecar.find("\"crop\""), std::string::npos);
            }
        }
    }
    // Derived seeds in the manifest match the documented derivation.
    EXPECT_NE(manifest[1].find("\"data_seed\":" + std::to_string(DeriveDataSeed(42, Modulation::BPSK, 0))),
              std::string::npos);
    EXPECT_NE(manifest[1].find("\"noise_seed\":" + std::to_string(DeriveNoiseSeed(42, Modulation::BPSK, 0, 5490))),
              std::string::npos);
    // Paired SNR points share data and noise derivations.
    const auto seed_field = [](const std::string &record, const char *key) {
        const auto start = record.find(key) + std::string(key).size();
        return record.substr(start, record.find(',', start) - start);
    };
    EXPECT_EQ(seed_field(manifest[1], "\"data_seed\":"), seed_field(manifest[3], "\"data_seed\":"));
    EXPECT_EQ(seed_field(manifest[1], "\"noise_seed\":"), seed_field(manifest[3], "\"noise_seed\":"));
    std::filesystem::remove_all(dir);
}

TEST(Batch, CsvFramesHaveHeaderAndExactRows) {
    const auto dir = UniqueDir("iq-batch-csv-");
    BatchRequest request;
    request.FrameSize = 32;
    request.Format = ExportFormat::CSV;
    request.OutputDir = dir;
    RunBatch(request);
    const auto csv = ReadFile(dir / "frame_000000_000000.csv");
    std::istringstream input(csv);
    std::string line;
    std::getline(input, line);
    EXPECT_EQ(line, "time_s,i,q");
    std::size_t rows = 0;
    double previous_time = -1;
    while (std::getline(input, line)) {
        ASSERT_FALSE(line.empty());
        const auto time = std::stod(line.substr(0, line.find(',')));
        EXPECT_GT(time, previous_time); // Frame timestamps start at zero and increase.
        previous_time = time;
        ++rows;
    }
    EXPECT_EQ(rows, 32u);
    EXPECT_DOUBLE_EQ(previous_time, 31. / 8000.);
    std::filesystem::remove_all(dir);
}

TEST(Batch, ValidationRejectsBadRequestsBeforeOutput) {
    const auto dir = UniqueDir("iq-batch-invalid-");
    BatchRequest request;
    request.OutputDir = dir;
    // Existing directory is never touched.
    std::filesystem::create_directory(dir);
    EXPECT_THROW(RunBatch(request), std::runtime_error);
    EXPECT_TRUE(std::filesystem::is_empty(dir));
    std::filesystem::remove_all(dir);
    // Duplicates on any axis.
    request.Waveforms = {Modulation::BPSK, Modulation::BPSK};
    EXPECT_THROW(ValidateBatch(request), std::invalid_argument);
    request.Waveforms = {};
    request.Seeds = {7, 7};
    EXPECT_THROW(ValidateBatch(request), std::invalid_argument);
    request.Seeds = {};
    request.SnrsDb = {0, 10, 0};
    EXPECT_THROW(ValidateBatch(request), std::invalid_argument);
    request.SnrsDb = {NAN};
    EXPECT_THROW(ValidateBatch(request), std::invalid_argument);
    request.SnrsDb = {};
    // Explicit-bit input is rejected.
    request.Base.DataSource = DataSource::Explicit;
    request.Base.Bits.assign(256, '0');
    EXPECT_THROW(ValidateBatch(request), std::invalid_argument);
    request.Base.DataSource = DataSource::Random;
    request.Base.Bits.clear();
    // Frame accounting limits.
    request.FramesPerPoint = 0;
    EXPECT_THROW(ValidateBatch(request), std::invalid_argument);
    request.FramesPerPoint = 100001;
    EXPECT_THROW(ValidateBatch(request), std::length_error);
    request.FramesPerPoint = 1;
    request.FrameSize = 0;
    EXPECT_THROW(ValidateBatch(request), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(dir));
}

TEST(Batch, InvalidAwgnSweepLeavesNoOutput) {
    const auto dir = UniqueDir("iq-batch-preflight-");
    BatchRequest request;
    request.OutputDir = dir;
    // A valid first point must not be written before rejecting a later point.
    request.SnrsDb = {10, 4000};
    EXPECT_THROW(RunBatch(request), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(dir));
    request.SnrsDb = {10, -4000};
    EXPECT_THROW(RunBatch(request), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(dir));
    request.SnrsDb = {10};
    request.Base.AmplitudeGain = 0;
    EXPECT_THROW(RunBatch(request), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(dir));
    request.SnrsDb.clear();
    request.Base.Awgn.Enabled = true;
    EXPECT_THROW(RunBatch(request), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(dir));
}

TEST(Batch, PresetNoiseSettingRetainedWithoutSnrAxis) {
    const auto dir = UniqueDir("iq-batch-preset-");
    BatchRequest request;
    request.Base.Awgn.Enabled = true;
    request.Base.Awgn.SnrDb = 5;
    request.FrameSize = 64;
    request.OutputDir = dir;
    RunBatch(request);
    const auto sidecar = ReadFile(dir / "frame_000000_000000.sigmf-meta");
    EXPECT_NE(sidecar.find("\"requested_snr_db\": 5"), std::string::npos);
    EXPECT_NE(sidecar.find("\"snr_db\": null"), std::string::npos);
    const auto manifest = ReadLines(dir / "manifest.jsonl");
    EXPECT_NE(manifest[1].find("\"snr_db\":null"), std::string::npos);
    EXPECT_NE(manifest[1].find("\"requested_snr_db\":5"), std::string::npos);
    std::filesystem::remove_all(dir);
}

#ifdef __linux__
TEST(Batch, WriteFailurePreservesCompletedFrames) {
    const auto dir = UniqueDir("iq-batch-write-failure-");
    // Restrict file size in a child: frame files fit, but the manifest eventually
    // exceeds the limit. This exercises real stream failures without filling disk.
    ASSERT_EXIT(
        {
            rlimit limit{};
            limit.rlim_cur = 4096;
            limit.rlim_max = 4096;
            if (setrlimit(RLIMIT_FSIZE, &limit) != 0) {
                std::_Exit(2);
            }
            std::signal(SIGXFSZ, SIG_IGN);
            BatchRequest request;
            request.OutputDir = dir;
            request.FrameSize = 32;
            request.FramesPerPoint = 100;
            try {
                RunBatch(request);
            } catch (const std::ios_base::failure &) {
                std::_Exit(0);
            } catch (...) {
                std::_Exit(3);
            }
            std::_Exit(1);
        },
        ::testing::ExitedWithCode(0), "");
    const auto lines = ReadLines(dir / "manifest.jsonl");
    std::size_t completed = 0;
    for (const auto &line : lines) {
        EXPECT_EQ(line.find("\"kind\":\"summary\""), std::string::npos);
        if (line.find("\"kind\":\"frame\"") == std::string::npos || line.back() != '}') {
            continue;
        }
        std::ostringstream name;
        name << "frame_000000_" << std::setw(6) << std::setfill('0') << completed << ".sigmf-data";
        EXPECT_EQ(std::filesystem::file_size(dir / name.str()), 32u * 8u);
        const auto sidecar = ReadFile(MetadataPath(dir / name.str()));
        EXPECT_NE(sidecar.find("\"kind\": \"batch_frame\""), std::string::npos);
        ++completed;
    }
    EXPECT_GT(completed, 0u);
    EXPECT_LT(completed, 100u);
    std::filesystem::remove_all(dir);
}
#endif

TEST(Batch, SigmfFramesCarryMetadataAndAnalyze) {
    const auto dir = UniqueDir("iq-batch-sigmf-");
    BatchRequest request;
    request.Waveforms = {Modulation::QPSK};
    request.SnrsDb = {10};
    request.FrameSize = 2048;
    request.OutputDir = dir;
    ASSERT_EQ(request.Format, ExportFormat::SigMF);
    RunBatch(request);
    EXPECT_EQ(std::filesystem::file_size(dir / "frame_000000_000000.sigmf-data"), 2048u * 8);
    EXPECT_FALSE(std::filesystem::exists(dir / "frame_000000_000000.sigmf-data.json"));
    const auto meta = nlohmann::json::parse(ReadFile(dir / "frame_000000_000000.sigmf-meta"));
    EXPECT_EQ(meta.at("global").at("core:datatype"), "cf32_le");
    EXPECT_EQ(meta.at("global").at("siggen:metadata").at("kind"), "batch_frame");
    EXPECT_EQ(meta.at("global").at("siggen:metadata").at("axes").at("snr_db"), 10);
    const auto manifest = ReadLines(dir / "manifest.jsonl");
    EXPECT_NE(manifest[0].find("\"format\":\"sigmf\""), std::string::npos);
    EXPECT_NE(manifest[1].find("\"sidecar\":\"frame_000000_000000.sigmf-meta\""), std::string::npos);
    const auto recording = ReadRecording(dir / "frame_000000_000000.sigmf-meta");
    EXPECT_EQ(recording.Samples.size(), 2048u);
    ASSERT_TRUE(recording.Config.has_value());
    ASSERT_TRUE(recording.Frame.has_value());
    EXPECT_EQ(recording.Frame->Size, 2048u);
    // EVM is scored on the frame interior only, so it tracks the requested 10 dB per-sample SNR
    const auto report = AnalyzeRecording(recording);
    ASSERT_TRUE(report.Accuracy.has_value());
    EXPECT_GT(report.Accuracy->SymbolCount, 100u);
    EXPECT_LT(report.Accuracy->SymbolCount, 256u);
    EXPECT_NEAR(report.SampleSnrDb, 10.0, 1.5);
}

TEST(Batch, CleanSigmfFrameScoresAlmostZeroEvm) {
    const auto dir = UniqueDir("iq-batch-sigmf-clean-");
    BatchRequest request;
    request.Waveforms = {Modulation::QAM16};
    request.FrameSize = 1024;
    request.Base.Awgn.Enabled = false;
    request.OutputDir = dir;
    RunBatch(request);
    const auto report = AnalyzeRecording(ReadRecording(dir / "frame_000000_000000.sigmf-meta"));
    ASSERT_TRUE(report.Accuracy.has_value());
    EXPECT_LT(report.Accuracy->EvmRms, 0.01);
}

TEST(Batch, WgnSigmfFrameHasNoEvm) {
    const auto dir = UniqueDir("iq-batch-sigmf-wgn-");
    BatchRequest request;
    request.Waveforms = {Modulation::WGN};
    request.FrameSize = 256;
    request.OutputDir = dir;
    RunBatch(request);
    const auto report = AnalyzeRecording(ReadRecording(dir / "frame_000000_000000.sigmf-meta"));
    EXPECT_FALSE(report.Accuracy.has_value());
}

} // namespace Core
