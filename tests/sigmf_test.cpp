#include "analysis.h"
#include "iq_export.h"
#include "measurements.h"
#include "preset.h"
#include "recording.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <random>

namespace {
std::filesystem::path scratch_dir(const std::string& name) {
    const auto dir = std::filesystem::temp_directory_path() / ("siggen-sigmf-" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}
iq::GenerationConfig qam_config() {
    iq::GenerationConfig c;
    c.modulation = iq::Modulation::QAM16;
    c.symbol_count = 256;
    c.samples_per_symbol = 8;
    c.awgn.enabled = true;
    c.awgn.snr_db = 20;
    return c;
}
void write_file(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream(path, std::ios::binary) << bytes;
}
}

TEST(SigMF, MetadataPathPairsDataAndMetaFiles) {
    EXPECT_EQ(iq::metadata_path("a/b.sigmf-data"), std::filesystem::path("a/b.sigmf-meta"));
    EXPECT_EQ(iq::metadata_path("a/b.csv"), std::filesystem::path("a/b.csv.json"));
}

TEST(SigMF, MetadataIsValidJsonWithCoreFields) {
    const auto signal = iq::generate(qam_config());
    const auto root = nlohmann::json::parse(iq::export_metadata(signal, iq::ExportFormat::SigMF));
    const auto& global = root.at("global");
    EXPECT_EQ(global.at("core:datatype"), "cf32_le");
    EXPECT_DOUBLE_EQ(global.at("core:sample_rate").get<double>(), signal.sample_rate_hz);
    EXPECT_EQ(global.at("core:version"), "1.0.0");
    EXPECT_EQ(global.at("siggen:metadata").at("sample_count").get<std::size_t>(), signal.samples.size());
    EXPECT_EQ(global.at("siggen:preset"), iq::serialize_preset(signal.config));
    EXPECT_EQ(root.at("captures").size(), 1u);
}

TEST(SigMF, ExportThenReadRoundTripsSamplesAndConfiguration) {
    const auto dir = scratch_dir("roundtrip");
    const auto signal = iq::generate(qam_config());
    iq::export_signal(dir / "tx.sigmf-data", signal, iq::ExportFormat::SigMF);
    ASSERT_TRUE(std::filesystem::exists(dir / "tx.sigmf-meta"));
    EXPECT_EQ(std::filesystem::file_size(dir / "tx.sigmf-data"), signal.samples.size() * 8);
    for (const auto& path : {dir / "tx.sigmf-meta", dir / "tx.sigmf-data"}) {
        const auto recording = iq::read_recording(path);
        ASSERT_EQ(recording.samples.size(), signal.samples.size());
        for (std::size_t k = 0; k < signal.samples.size(); ++k) ASSERT_EQ(recording.samples[k], signal.samples[k]);
        EXPECT_DOUBLE_EQ(recording.sample_rate_hz, signal.sample_rate_hz);
        ASSERT_TRUE(recording.config.has_value());
        EXPECT_EQ(iq::serialize_preset(*recording.config), iq::serialize_preset(signal.config));
    }
}

TEST(SigMF, ExportRefusesToReplaceWithoutOverwrite) {
    const auto dir = scratch_dir("overwrite");
    const auto signal = iq::generate(qam_config());
    iq::export_signal(dir / "tx.sigmf-data", signal, iq::ExportFormat::SigMF);
    EXPECT_THROW(iq::export_signal(dir / "tx.sigmf-data", signal, iq::ExportFormat::SigMF), std::runtime_error);
    EXPECT_NO_THROW(iq::export_signal(dir / "tx.sigmf-data", signal, iq::ExportFormat::SigMF, true));
}

TEST(SigMF, NoiseSourcesRoundTrip) {
    const auto dir = scratch_dir("wgn");
    iq::GenerationConfig c;
    c.modulation = iq::Modulation::WGN;
    const auto signal = iq::generate(c);
    iq::export_signal(dir / "n.sigmf-data", signal, iq::ExportFormat::SigMF);
    const auto recording = iq::read_recording(dir / "n.sigmf-meta");
    EXPECT_EQ(recording.samples.size(), signal.samples.size());
    const auto report = iq::analyze_recording(recording);
    EXPECT_FALSE(report.accuracy.has_value());
    EXPECT_FALSE(report.note.empty());
}

TEST(Analyze, EvmFromFileMatchesTheGeneratorsOwnMeasurement) {
    const auto dir = scratch_dir("evm");
    const auto signal = iq::generate(qam_config());
    iq::export_signal(dir / "tx.sigmf-data", signal, iq::ExportFormat::SigMF);
    const auto report = iq::analyze_recording(iq::read_recording(dir / "tx.sigmf-meta"));
    const auto expected = iq::symbol_accuracy(signal);
    ASSERT_TRUE(report.accuracy.has_value());
    ASSERT_TRUE(expected.has_value());
    EXPECT_DOUBLE_EQ(report.accuracy->evm_rms, expected->evm_rms);
    EXPECT_EQ(report.waveform, std::string("16-QAM"));
    // Post-filter SNR = sample SNR + 10 log10(SPS); the measurement tracks the requested 20 dB.
    EXPECT_NEAR(report.sample_snr_db, 20.0, 1.0);
    const auto stats = iq::power_statistics(signal.samples);
    EXPECT_DOUBLE_EQ(report.power.papr_db, stats.papr_db);
    EXPECT_DOUBLE_EQ(report.sample_rate_hz, signal.sample_rate_hz);
}

TEST(Analyze, EvmReflectsSamplesChangedAfterExport) {
    const auto dir = scratch_dir("edited");
    auto config = qam_config();
    config.awgn.enabled = false;
    auto signal = iq::generate(config);
    iq::export_signal(dir / "tx.sigmf-data", signal, iq::ExportFormat::SigMF);
    auto recording = iq::read_recording(dir / "tx.sigmf-meta");
    const auto clean = iq::analyze_recording(recording);
    std::mt19937 rng(1);
    std::normal_distribution<float> gauss(0.0f, 0.1f);
    for (auto& s : recording.samples) s += std::complex<float>(gauss(rng), gauss(rng));
    const auto noisy = iq::analyze_recording(recording);
    ASSERT_TRUE(clean.accuracy && noisy.accuracy);
    EXPECT_LT(clean.accuracy->evm_rms, 0.02);
    EXPECT_GT(noisy.accuracy->evm_rms, 5 * clean.accuracy->evm_rms);
}

TEST(Analyze, SpectrumFindsToneAndBandwidth) {
    iq::Recording recording;
    recording.sample_rate_hz = 8000;
    for (int k = 0; k < 8192; ++k) {
        const double phase = 2 * M_PI * 1000.0 * k / 8000;
        recording.samples.emplace_back(static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase)));
    }
    const auto report = iq::analyze_recording(recording);
    EXPECT_NEAR(report.peak_frequency_hz, 1000, 8000.0 / 1024);
    EXPECT_NEAR(report.power.mean_power, 1, 1e-6);
    EXPECT_NEAR(report.power.papr_db, 0, 1e-6);
    EXPECT_LT(report.occupied_bandwidth_hz, 100);
    EXPECT_FALSE(report.accuracy.has_value());
    EXPECT_NE(iq::report_text(report).find("PAPR"), std::string::npos);
    const auto json = nlohmann::json::parse(iq::report_json(report));
    EXPECT_EQ(json.at("sample_count"), 8192);
}

TEST(Recording, ReadsSigmfCi16WithScaling) {
    const auto dir = scratch_dir("ci16");
    write_file(dir / "r.sigmf-meta", R"({"global": {"core:datatype": "ci16_le", "core:sample_rate": 1000.0, "core:version": "1.0.0"}, "captures": [], "annotations": []})");
    // (16384, -32768) and (-1, 0) little-endian.
    write_file(dir / "r.sigmf-data", std::string("\x00\x40\x00\x80\xff\xff\x00\x00", 8));
    const auto recording = iq::read_recording(dir / "r.sigmf-meta");
    ASSERT_EQ(recording.samples.size(), 2u);
    EXPECT_FLOAT_EQ(recording.samples[0].real(), 0.5f);
    EXPECT_FLOAT_EQ(recording.samples[0].imag(), -1.0f);
    EXPECT_FLOAT_EQ(recording.samples[1].real(), -1.0f / 32768);
}

TEST(Recording, RejectsBadInputs) {
    const auto dir = scratch_dir("bad");
    write_file(dir / "u.sigmf-meta", R"({"global": {"core:datatype": "cu8", "core:sample_rate": 1000}})");
    write_file(dir / "u.sigmf-data", "abcd");
    EXPECT_THROW(iq::read_recording(dir / "u.sigmf-meta"), std::runtime_error);
    write_file(dir / "t.sigmf-meta", R"({"global": {"core:datatype": "cf32_le", "core:sample_rate": 1000}})");
    write_file(dir / "t.sigmf-data", std::string(10, 'x'));
    EXPECT_THROW(iq::read_recording(dir / "t.sigmf-meta"), std::runtime_error);
    write_file(dir / "r.sigmf-meta", R"({"global": {"core:datatype": "cf32_le"}})");
    EXPECT_THROW(iq::read_recording(dir / "r.sigmf-meta"), std::runtime_error);
    write_file(dir / "m.sigmf-meta", "not json");
    EXPECT_THROW(iq::read_recording(dir / "m.sigmf-meta"), std::invalid_argument);
    EXPECT_THROW(iq::read_recording(dir / "missing.sigmf-meta"), std::runtime_error);
    EXPECT_THROW(iq::read_recording(dir / "tx.iq"), std::runtime_error);
    write_file(dir / "nan.sigmf-meta", R"({"global": {"core:datatype": "cf32_le", "core:sample_rate": 1000}})");
    write_file(dir / "nan.sigmf-data", std::string("\x00\x00\xc0\x7f\x00\x00\x00\x00", 8));
    EXPECT_THROW(iq::read_recording(dir / "nan.sigmf-meta"), std::runtime_error);
    EXPECT_THROW(iq::analyze_recording(iq::Recording{}), std::invalid_argument);
}
