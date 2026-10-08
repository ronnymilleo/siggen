/**
 * @file    export_test.cpp
 * @brief   Tests for writing I/Q samples and their JSON metadata sidecars.
 */

#include "iq_export.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <span>
#include <sstream>

namespace Core {

namespace {

// Two rectangular BPSK samples, one per symbol: {1, 0} then {-1, 0}
GeneratedSignal Fixture() {
    GenerationConfig config;
    config.SymbolCount = 2;
    config.SamplesPerSymbol = 1;
    config.Pulse = Pulse::Rectangular;
    config.DataSource = DataSource::Explicit;
    config.Bits = "01";
    return Generate(config);
}

} // namespace

TEST(Export, CSVPrecisionCountAndMetadata) {
    GenerationConfig config;
    config.Modulation = Modulation::QPSK;
    const auto result = Generate(config);
    std::ostringstream out;
    WriteSamples(out, result, ExportFormat::CSV);
    std::istringstream input(out.str());
    std::string line;
    std::getline(input, line);
    EXPECT_EQ(line, "time_s,i,q");
    std::size_t k = 0;
    while (std::getline(input, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        double time;
        float i, q;
        ASSERT_TRUE(static_cast<bool>(row >> time >> i >> q));
        ASSERT_LT(k, result.Samples.size());
        EXPECT_DOUBLE_EQ(time, k / result.SampleRateHz);
        EXPECT_EQ(std::complex<float>(i, q), result.Samples[k++]);
    }
    EXPECT_EQ(k, result.Samples.size());
    auto metadata = ExportMetadata(result, ExportFormat::CSV);
    EXPECT_NE(metadata.find("\"sample_count\": " + std::to_string(k)), std::string::npos);
    EXPECT_NE(metadata.find("\"sample_rate_hz\": 8000"), std::string::npos);
    EXPECT_NE(metadata.find("\"filter_delay_samples\": 40"), std::string::npos);
    EXPECT_NE(metadata.find("\"modulation\": \"QPSK\""), std::string::npos);
}

TEST(Export, BinaryByteOrderAndWriteFailure) {
    auto result = Fixture();
    std::ostringstream out;
    WriteSamples(out, result, ExportFormat::SigMF);
    const unsigned char bytes[] = {0, 0, 128, 63, 0, 0, 0, 0, 0, 0, 128, 191, 0, 0, 0, 0};
    EXPECT_EQ(out.str(), std::string(reinterpret_cast<const char *>(bytes), sizeof bytes));
    std::ostringstream bad;
    bad.setstate(std::ios::badbit);
    EXPECT_THROW(WriteSamples(bad, result, ExportFormat::CSV), std::runtime_error);
    result.SampleRateHz = 1;
    EXPECT_THROW(ExportMetadata(result, ExportFormat::CSV), std::invalid_argument);
}

TEST(Export, FilePairAndOverwriteProtection) {
    auto dir = std::filesystem::temp_directory_path() /
               ("iq-export-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    const auto path = dir / "test.sigmf-data";
    const auto result = Fixture();
    ExportSignal(path, result, ExportFormat::SigMF);
    EXPECT_EQ(std::filesystem::file_size(path), 16u);
    EXPECT_TRUE(std::filesystem::exists(MetadataPath(path)));
    EXPECT_THROW(ExportSignal(path, result, ExportFormat::CSV), std::runtime_error);
    EXPECT_EQ(std::filesystem::file_size(path), 16u);
    EXPECT_NO_THROW(ExportSignal(path, result, ExportFormat::CSV, true));
    EXPECT_THROW(ExportSignal(dir / "missing" / "x", result, ExportFormat::CSV), std::ios_base::failure);
    std::filesystem::remove_all(dir);
}

TEST(Export, FlushFailureAndSidecarProtection) {
    struct FailFlush : std::stringbuf {
        int sync() override { return -1; }
    } buffer;
    std::ostream failing(&buffer);
    EXPECT_THROW(WriteSamples(failing, Fixture(), ExportFormat::CSV), std::runtime_error);
    const auto dir = std::filesystem::temp_directory_path() /
                     ("iq-sidecar-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    const auto path = dir / "signal";
    {
        std::ofstream existing(MetadataPath(path));
        existing << "keep";
    }
    EXPECT_THROW(ExportSignal(path, Fixture(), ExportFormat::CSV), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_EQ(std::filesystem::file_size(MetadataPath(path)), 4u);
    std::filesystem::remove_all(dir);
}

TEST(Export, Version2LinearAndAwgnMetadata) {
    GenerationConfig config;
    config.SymbolCount = 64;
    const auto clean = Generate(config);
    const auto clean_metadata = ExportMetadata(clean, ExportFormat::CSV);
    EXPECT_NE(clean_metadata.find("\"version\": 2"), std::string::npos);
    EXPECT_NE(clean_metadata.find("\"family\": \"linear\""), std::string::npos);
    EXPECT_NE(clean_metadata.find("\"waveform\": \"BPSK\""), std::string::npos);
    EXPECT_NE(clean_metadata.find("\"symbol_energy\": 1"), std::string::npos);
    EXPECT_EQ(clean_metadata.find("\"awgn\""), std::string::npos);
    config.Awgn.Enabled = true;
    config.Awgn.SnrDb = 10;
    const auto noisy = Generate(config);
    const auto metadata = ExportMetadata(noisy, ExportFormat::SigMF);
    EXPECT_NE(metadata.find("\"awgn\""), std::string::npos);
    EXPECT_NE(metadata.find("\"requested_snr_db\": 10"), std::string::npos);
    EXPECT_NE(metadata.find("\"reference_interval\": {\"begin\": 80, \"end\": 512}"), std::string::npos);
    EXPECT_NE(metadata.find("\"added_noise_power\""), std::string::npos);
    EXPECT_NE(metadata.find("\"noise_seed\": 5490"), std::string::npos);
    // Byte layout is unchanged: little-endian float32 I/Q pairs.
    std::ostringstream out;
    WriteSamples(out, noisy, ExportFormat::SigMF);
    EXPECT_EQ(out.str().size(), noisy.Samples.size() * 8);
}

TEST(Export, NoiseFamilyMetadataOmitsSymbolClaims) {
    GenerationConfig config;
    config.Modulation = Modulation::WGN;
    config.NoiseSource.SampleCount = 16;
    auto result = Generate(config);
    std::ostringstream out;
    WriteSamples(out, result, ExportFormat::CSV);
    const auto text = out.str();
    EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 17u); // header + 16 rows
    const auto metadata = ExportMetadata(result, ExportFormat::CSV);
    EXPECT_NE(metadata.find("\"version\": 2"), std::string::npos);
    EXPECT_NE(metadata.find("\"family\": \"noise\""), std::string::npos);
    EXPECT_NE(metadata.find("\"waveform\": \"WGN\""), std::string::npos);
    EXPECT_NE(metadata.find("\"noise_source\""), std::string::npos);
    EXPECT_NE(metadata.find("\"noise_seed\": 5490"), std::string::npos);
    EXPECT_EQ(metadata.find("symbol_energy"), std::string::npos);
    EXPECT_EQ(metadata.find("\"bits\""), std::string::npos);
    EXPECT_EQ(metadata.find("random_bit_rule"), std::string::npos);
    // Length validation distinguishes noise results from linear trains.
    result.Samples.pop_back();
    EXPECT_THROW(ExportMetadata(result, ExportFormat::CSV), std::invalid_argument);
    result.Config.NoiseSource.SampleCount = 15;
    EXPECT_NO_THROW(ExportMetadata(result, ExportFormat::CSV));
}

TEST(Export, RawSpanWriterMatchesSignalWriter) {
    const auto result = Fixture();
    std::ostringstream direct, via_signal;
    WriteSamples(direct, std::span<const std::complex<float>>(result.Samples), result.SampleRateHz,
                 ExportFormat::SigMF);
    WriteSamples(via_signal, result, ExportFormat::SigMF);
    EXPECT_EQ(direct.str(), via_signal.str());
    std::ostringstream bad;
    EXPECT_THROW(WriteSamples(bad, std::span<const std::complex<float>>(result.Samples), 0, ExportFormat::CSV),
                 std::invalid_argument);
}

} // namespace Core
