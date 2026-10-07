#include "batch.h"
#include "analysis.h"
#include "recording.h"
#include <nlohmann/json.hpp>
#include "noise.h"
#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#ifdef __linux__
#include <csignal>
#include <cstdlib>
#include <sys/resource.h>
#endif

using namespace iq;

namespace {
std::filesystem::path unique_dir(const char* prefix) {
    return std::filesystem::temp_directory_path() /
           (std::string(prefix) + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}
std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::vector<std::string> lines;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line))
        if (!line.empty()) lines.push_back(line);
    return lines;
}
}

TEST(Batch, SeedDerivationDeterministicAndSnrIndependent) {
    const auto data = derive_data_seed(42, Modulation::BPSK, 3);
    EXPECT_EQ(data, derive_data_seed(42, Modulation::BPSK, 3));
    EXPECT_NE(data, derive_data_seed(42, Modulation::BPSK, 4));
    EXPECT_NE(data, derive_data_seed(43, Modulation::BPSK, 3));
    EXPECT_NE(data, derive_data_seed(42, Modulation::QPSK, 3));
    EXPECT_NE(data, derive_noise_seed(42, Modulation::BPSK, 3, 5490));
    // The configured noise seed participates in noise derivation only.
    EXPECT_EQ(derive_noise_seed(42, Modulation::BPSK, 3, 5490), derive_noise_seed(42, Modulation::BPSK, 3, 5490));
    EXPECT_NE(derive_noise_seed(42, Modulation::BPSK, 3, 5490), derive_noise_seed(42, Modulation::BPSK, 3, 5491));
    EXPECT_EQ(derive_data_seed(42, Modulation::BPSK, 3), derive_data_seed(42, Modulation::BPSK, 3));
}

TEST(Batch, FrameGeometryRRCAndRectangular) {
    GenerationConfig base;
    const int frame_size = 256;
    const auto rrc = generate_frame(base, frame_size, 0, std::nullopt);
    EXPECT_EQ(rrc.samples.size(), 256u);
    EXPECT_EQ(rrc.filter_delay_samples, 40u);
    EXPECT_EQ(rrc.crop_offset, 10u * 8u + 40u); // span*SPS + filter delay.
    EXPECT_FALSE(rrc.noise.awgn_applied);
    // The frame equals the middle of the guarded full generation with the derived seed.
    auto full_config = base;
    full_config.seed = rrc.data_seed;
    full_config.symbol_count = 256 / 8 + 2 * 10; // N + 2G
    const auto full = generate(full_config);
    ASSERT_GE(full.samples.size(), rrc.crop_offset + 256u);
    for (std::size_t k = 0; k < 256; ++k)
        EXPECT_EQ(rrc.samples[k], full.samples[rrc.crop_offset + k]);

    base.pulse = Pulse::Rectangular;
    const auto rect = generate_frame(base, frame_size, 0, std::nullopt);
    EXPECT_EQ(rect.samples.size(), 256u);
    EXPECT_EQ(rect.crop_offset, 0u);
    EXPECT_EQ(rect.filter_delay_samples, 0u);
    // Frame size not divisible by SPS still yields exact length.
    const auto odd = generate_frame(base, 100, 0, std::nullopt);
    EXPECT_EQ(odd.samples.size(), 100u);
}

TEST(Batch, WGNFramesIgnoreSnrAxis) {
    GenerationConfig base;
    base.modulation = Modulation::WGN;
    base.noise_source.sample_count = 999; // Overridden by the frame size.
    const auto frame = generate_frame(base, 128, 0, std::optional<double>(10));
    EXPECT_EQ(frame.samples.size(), 128u);
    EXPECT_FALSE(frame.noise.awgn_applied);
    EXPECT_EQ(frame.crop_offset, 0u);
    EXPECT_DOUBLE_EQ(frame.sample_rate_hz, base.noise_source.sample_rate_hz);
    // The frame is exactly the seeded WGN stream at configured power.
    const auto expected = gaussian_noise(128, base.noise_source.noise_power, frame.noise_seed);
    EXPECT_EQ(frame.samples, expected);
}

TEST(Batch, FrameAwgnAppliesAfterCrop) {
    GenerationConfig base;
    base.symbol_count = 512; // Ignored; frame size fixes length.
    const auto clean = generate_frame(base, 256, 0, std::nullopt);
    const auto noisy = generate_frame(base, 256, 0, std::optional<double>(10));
    EXPECT_EQ(clean.data_seed, noisy.data_seed);
    EXPECT_EQ(clean.noise_seed, noisy.noise_seed);
    EXPECT_TRUE(noisy.noise.awgn_applied);
    EXPECT_EQ(noisy.noise.reference_begin, 0u);
    EXPECT_EQ(noisy.noise.reference_end, 256u);
    double reference = 0;
    for (const auto& sample : clean.samples) reference += std::norm(sample);
    reference /= 256;
    EXPECT_NEAR(noisy.noise.reference_power, reference, 1e-9);
    const auto noise = gaussian_noise(256, noisy.noise.added_noise_power, noisy.noise_seed);
    for (std::size_t k = 0; k < 256; ++k)
        EXPECT_FLOAT_EQ((clean.samples[k] + noise[k]).real(), noisy.samples[k].real());
    // SNR does not enter seed derivation: realizations stay paired.
    const auto louder = generate_frame(base, 256, 0, std::optional<double>(20));
    EXPECT_EQ(louder.data_seed, noisy.data_seed);
    EXPECT_EQ(louder.noise_seed, noisy.noise_seed);
}

TEST(Batch, FrameLimitsAndInvalidRequests) {
    GenerationConfig base;
    EXPECT_THROW(generate_frame(base, 0, 0, std::nullopt), std::invalid_argument);
    EXPECT_THROW(validate_frame_request(base, static_cast<Modulation>(99), 64), std::invalid_argument);
    // Rectangular SPS=1 frames are limited by the 65536-symbol payload bound.
    base.pulse = Pulse::Rectangular;
    base.samples_per_symbol = 1;
    EXPECT_THROW(validate_frame_request(base, Modulation::BPSK, 65537), std::length_error);
    EXPECT_NO_THROW(validate_frame_request(base, Modulation::BPSK, 65536));
    // RRC guards count toward the symbol limit.
    base = {};
    base.samples_per_symbol = 2;
    base.span_symbols = 100;
    EXPECT_THROW(validate_frame_request(base, Modulation::BPSK, 200000), std::length_error);
}

TEST(Batch, RunWritesFramesSidecarsAndManifest) {
    const auto dir = unique_dir("iq-batch-run-");
    BatchRequest request;
    request.format = ExportFormat::BinaryFloat32;
    request.waveforms = {Modulation::BPSK, Modulation::WGN};
    request.seeds = {42};
    request.snrs_db = {0, 10};
    request.frame_size = 64;
    request.frames_per_point = 2;
    request.output_dir = dir;
    const auto summary = run_batch(request);
    // BPSK has two SNR points, WGN ignores the SNR axis: three points, six frames.
    EXPECT_EQ(summary.point_count, 3u);
    EXPECT_EQ(summary.frame_count, 6u);
    const auto manifest = read_lines(dir / "manifest.jsonl");
    ASSERT_EQ(manifest.size(), 8u); // header + six frames + summary
    EXPECT_NE(manifest.front().find("\"kind\":\"batch_header\""), std::string::npos);
    EXPECT_NE(manifest.front().find("\"manifest_version\":1"), std::string::npos);
    EXPECT_NE(manifest.front().find("\"waveforms\":[\"BPSK\",\"WGN\"]"), std::string::npos);
    EXPECT_NE(manifest.back().find("\"kind\":\"summary\""), std::string::npos);
    EXPECT_NE(manifest.back().find("\"completed\":true"), std::string::npos);
    EXPECT_NE(manifest.back().find("\"frame_count\":6"), std::string::npos);
    // User list order is preserved: BPSK points first, then WGN with a null SNR.
    for (std::size_t k = 1; k <= 4; ++k)
        EXPECT_NE(manifest[k].find("\"waveform\":\"BPSK\""), std::string::npos) << k;
    for (std::size_t k = 5; k <= 6; ++k)
        EXPECT_NE(manifest[k].find("\"waveform\":\"WGN\""), std::string::npos) << k;
    EXPECT_NE(manifest[5].find("\"snr_db\":null"), std::string::npos);
    EXPECT_NE(manifest[1].find("\"snr_db\":0"), std::string::npos);
    EXPECT_NE(manifest[3].find("\"snr_db\":10"), std::string::npos);
    // Deterministic indexed filenames, binary default, exact byte size.
    for (std::size_t point = 0; point < 3; ++point)
        for (std::size_t frame = 0; frame < 2; ++frame) {
            std::ostringstream name;
            name << "frame_" << std::string(6 - std::to_string(point).size(), '0') << point << '_'
                 << std::string(6 - std::to_string(frame).size(), '0') << frame << ".cf32";
            const auto data = dir / name.str();
            ASSERT_TRUE(std::filesystem::exists(data)) << name.str();
            EXPECT_EQ(std::filesystem::file_size(data), 64u * 8u);
            const auto sidecar = read_file(dir / (name.str() + ".json"));
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
    // Derived seeds in the manifest match the documented derivation.
    EXPECT_NE(manifest[1].find("\"data_seed\":" + std::to_string(derive_data_seed(42, Modulation::BPSK, 0))),
              std::string::npos);
    EXPECT_NE(manifest[1].find("\"noise_seed\":" + std::to_string(derive_noise_seed(42, Modulation::BPSK, 0, 5490))),
              std::string::npos);
    // Paired SNR points share data and noise derivations.
    const auto seed_field = [](const std::string& record, const char* key) {
        const auto start = record.find(key) + std::string(key).size();
        return record.substr(start, record.find(',', start) - start);
    };
    EXPECT_EQ(seed_field(manifest[1], "\"data_seed\":"), seed_field(manifest[3], "\"data_seed\":"));
    EXPECT_EQ(seed_field(manifest[1], "\"noise_seed\":"), seed_field(manifest[3], "\"noise_seed\":"));
    std::filesystem::remove_all(dir);
}

TEST(Batch, CsvFramesHaveHeaderAndExactRows) {
    const auto dir = unique_dir("iq-batch-csv-");
    BatchRequest request;
    request.frame_size = 32;
    request.format = ExportFormat::CSV;
    request.output_dir = dir;
    run_batch(request);
    const auto csv = read_file(dir / "frame_000000_000000.csv");
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
    const auto dir = unique_dir("iq-batch-invalid-");
    BatchRequest request;
    request.output_dir = dir;
    // Existing directory is never touched.
    std::filesystem::create_directory(dir);
    EXPECT_THROW(run_batch(request), std::runtime_error);
    EXPECT_TRUE(std::filesystem::is_empty(dir));
    std::filesystem::remove_all(dir);
    // Duplicates on any axis.
    request.waveforms = {Modulation::BPSK, Modulation::BPSK};
    EXPECT_THROW(validate_batch(request), std::invalid_argument);
    request.waveforms = {};
    request.seeds = {7, 7};
    EXPECT_THROW(validate_batch(request), std::invalid_argument);
    request.seeds = {};
    request.snrs_db = {0, 10, 0};
    EXPECT_THROW(validate_batch(request), std::invalid_argument);
    request.snrs_db = {NAN};
    EXPECT_THROW(validate_batch(request), std::invalid_argument);
    request.snrs_db = {};
    // Explicit-bit input is rejected.
    request.base.data_source = DataSource::Explicit;
    request.base.bits.assign(256, '0');
    EXPECT_THROW(validate_batch(request), std::invalid_argument);
    request.base.data_source = DataSource::Random;
    request.base.bits.clear();
    // Frame accounting limits.
    request.frames_per_point = 0;
    EXPECT_THROW(validate_batch(request), std::invalid_argument);
    request.frames_per_point = 100001;
    EXPECT_THROW(validate_batch(request), std::length_error);
    request.frames_per_point = 1;
    request.frame_size = 0;
    EXPECT_THROW(validate_batch(request), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(dir));
}

TEST(Batch, InvalidAwgnSweepLeavesNoOutput) {
    const auto dir = unique_dir("iq-batch-preflight-");
    BatchRequest r;
    r.output_dir = dir;
    // A valid first point must not be written before rejecting a later point.
    r.snrs_db = {10, 4000};
    EXPECT_THROW(run_batch(r), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(dir));
    r.snrs_db = {10, -4000};
    EXPECT_THROW(run_batch(r), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(dir));
    r.snrs_db = {10};
    r.base.amplitude_gain = 0;
    EXPECT_THROW(run_batch(r), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(dir));
    r.snrs_db.clear();
    r.base.awgn.enabled = true;
    EXPECT_THROW(run_batch(r), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(dir));
}

TEST(Batch, PresetNoiseSettingRetainedWithoutSnrAxis) {
    const auto dir = unique_dir("iq-batch-preset-");
    BatchRequest request;
    request.format = ExportFormat::BinaryFloat32;
    request.base.awgn.enabled = true;
    request.base.awgn.snr_db = 5;
    request.frame_size = 64;
    request.output_dir = dir;
    run_batch(request);
    const auto sidecar = read_file(dir / "frame_000000_000000.cf32.json");
    EXPECT_NE(sidecar.find("\"requested_snr_db\": 5"), std::string::npos);
    EXPECT_NE(sidecar.find("\"snr_db\": null"), std::string::npos);
    const auto manifest = read_lines(dir / "manifest.jsonl");
    EXPECT_NE(manifest[1].find("\"snr_db\":null"), std::string::npos);
    EXPECT_NE(manifest[1].find("\"requested_snr_db\":5"), std::string::npos);
    std::filesystem::remove_all(dir);
}

#ifdef __linux__
TEST(Batch, WriteFailurePreservesCompletedFrames) {
    const auto dir = unique_dir("iq-batch-write-failure-");
    // Restrict file size in a child: frame files fit, but the manifest eventually
    // exceeds the limit. This exercises real stream failures without filling disk.
    ASSERT_EXIT({
        rlimit limit{};
        limit.rlim_cur = 4096;
        limit.rlim_max = 4096;
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0) std::_Exit(2);
        std::signal(SIGXFSZ, SIG_IGN);
        BatchRequest request;
        request.format = ExportFormat::BinaryFloat32;
        request.output_dir = dir;
        request.frame_size = 32;
        request.frames_per_point = 100;
        try { run_batch(request); }
        catch (const std::ios_base::failure&) { std::_Exit(0); }
        catch (...) { std::_Exit(3); }
        std::_Exit(1);
    }, ::testing::ExitedWithCode(0), "");
    const auto lines = read_lines(dir / "manifest.jsonl");
    std::size_t completed = 0;
    for (const auto& line : lines) {
        EXPECT_EQ(line.find("\"kind\":\"summary\""), std::string::npos);
        if (line.find("\"kind\":\"frame\"") == std::string::npos || line.back() != '}') continue;
        std::ostringstream name;
        name << "frame_000000_" << std::setw(6) << std::setfill('0') << completed << ".cf32";
        EXPECT_EQ(std::filesystem::file_size(dir / name.str()), 32u * 8u);
        const auto sidecar = read_file(dir / (name.str() + ".json"));
        EXPECT_NE(sidecar.find("\"kind\": \"batch_frame\""), std::string::npos);
        ++completed;
    }
    EXPECT_GT(completed, 0u);
    EXPECT_LT(completed, 100u);
    std::filesystem::remove_all(dir);
}
#endif

TEST(Batch, SigmfFramesCarryMetadataAndAnalyze) {
    const auto dir = unique_dir("iq-batch-sigmf-");
    BatchRequest request;
    request.waveforms = {Modulation::QPSK};
    request.snrs_db = {10};
    request.frame_size = 256;
    request.output_dir = dir;
    ASSERT_EQ(request.format, ExportFormat::SigMF);
    run_batch(request);
    EXPECT_EQ(std::filesystem::file_size(dir / "frame_000000_000000.sigmf-data"), 256u * 8);
    EXPECT_FALSE(std::filesystem::exists(dir / "frame_000000_000000.sigmf-data.json"));
    const auto meta = nlohmann::json::parse(read_file(dir / "frame_000000_000000.sigmf-meta"));
    EXPECT_EQ(meta.at("global").at("core:datatype"), "cf32_le");
    EXPECT_EQ(meta.at("global").at("siggen:metadata").at("kind"), "batch_frame");
    EXPECT_EQ(meta.at("global").at("siggen:metadata").at("axes").at("snr_db"), 10);
    const auto manifest = read_lines(dir / "manifest.jsonl");
    EXPECT_NE(manifest[0].find("\"format\":\"sigmf\""), std::string::npos);
    EXPECT_NE(manifest[1].find("\"sidecar\":\"frame_000000_000000.sigmf-meta\""), std::string::npos);
    const auto recording = read_recording(dir / "frame_000000_000000.sigmf-meta");
    EXPECT_EQ(recording.samples.size(), 256u);
    EXPECT_FALSE(recording.config.has_value());
    EXPECT_NO_THROW(analyze_recording(recording));
}
