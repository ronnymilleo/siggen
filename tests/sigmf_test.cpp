/**
 * @file    sigmf_test.cpp
 * @brief   Tests for SigMF export, reading recordings back and analyzing them.
 */

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

namespace Core {

namespace {

std::filesystem::path ScratchDir(const std::string &name) {
    const auto dir = std::filesystem::temp_directory_path() / ("siggen-sigmf-" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

GenerationConfig QamConfig() {
    GenerationConfig c;
    c.Modulation = Modulation::QAM16;
    c.SymbolCount = 256;
    c.SamplesPerSymbol = 8;
    c.Awgn.Enabled = true;
    c.Awgn.SnrDb = 20;
    return c;
}

void WriteFile(const std::filesystem::path &path, const std::string &bytes) {
    std::ofstream(path, std::ios::binary) << bytes;
}

} // namespace

TEST(SigMF, MetadataPathPairsDataAndMetaFiles) {
    EXPECT_EQ(MetadataPath("a/b.sigmf-data"), std::filesystem::path("a/b.sigmf-meta"));
    EXPECT_EQ(MetadataPath("a/b.csv"), std::filesystem::path("a/b.csv.json"));
}

TEST(SigMF, MetadataIsValidJsonWithCoreFields) {
    const auto signal = Generate(QamConfig());
    const auto root = nlohmann::json::parse(ExportMetadata(signal, ExportFormat::SigMF));
    const auto &global = root.at("global");
    EXPECT_EQ(global.at("core:datatype"), "cf32_le");
    EXPECT_DOUBLE_EQ(global.at("core:sample_rate").get<double>(), signal.SampleRateHz);
    EXPECT_EQ(global.at("core:version"), "1.0.0");
    EXPECT_EQ(global.at("siggen:metadata").at("sample_count").get<std::size_t>(), signal.Samples.size());
    EXPECT_EQ(global.at("siggen:preset"), SerializePreset(signal.Config));
    EXPECT_EQ(root.at("captures").size(), 1u);
}

TEST(SigMF, ExportThenReadRoundTripsSamplesAndConfiguration) {
    const auto dir = ScratchDir("roundtrip");
    const auto signal = Generate(QamConfig());
    ExportSignal(dir / "tx.sigmf-data", signal, ExportFormat::SigMF);
    ASSERT_TRUE(std::filesystem::exists(dir / "tx.sigmf-meta"));
    EXPECT_EQ(std::filesystem::file_size(dir / "tx.sigmf-data"), signal.Samples.size() * 8);
    for (const auto &path : {dir / "tx.sigmf-meta", dir / "tx.sigmf-data"}) {
        const auto recording = ReadRecording(path);
        ASSERT_EQ(recording.Samples.size(), signal.Samples.size());
        for (std::size_t k = 0; k < signal.Samples.size(); ++k) {
            ASSERT_EQ(recording.Samples[k], signal.Samples[k]);
        }
        EXPECT_DOUBLE_EQ(recording.SampleRateHz, signal.SampleRateHz);
        ASSERT_TRUE(recording.Config.has_value());
        EXPECT_EQ(SerializePreset(*recording.Config), SerializePreset(signal.Config));
    }
}

TEST(SigMF, ExportRefusesToReplaceWithoutOverwrite) {
    const auto dir = ScratchDir("overwrite");
    const auto signal = Generate(QamConfig());
    ExportSignal(dir / "tx.sigmf-data", signal, ExportFormat::SigMF);
    EXPECT_THROW(ExportSignal(dir / "tx.sigmf-data", signal, ExportFormat::SigMF), std::runtime_error);
    EXPECT_NO_THROW(ExportSignal(dir / "tx.sigmf-data", signal, ExportFormat::SigMF, true));
}

TEST(SigMF, NoiseSourcesRoundTrip) {
    const auto dir = ScratchDir("wgn");
    GenerationConfig c;
    c.Modulation = Modulation::WGN;
    const auto signal = Generate(c);
    ExportSignal(dir / "n.sigmf-data", signal, ExportFormat::SigMF);
    const auto recording = ReadRecording(dir / "n.sigmf-meta");
    EXPECT_EQ(recording.Samples.size(), signal.Samples.size());
    const auto report = AnalyzeRecording(recording);
    EXPECT_FALSE(report.Accuracy.has_value());
    EXPECT_FALSE(report.Note.empty());
}

TEST(Analyze, EvmFromFileMatchesTheGeneratorsOwnMeasurement) {
    const auto dir = ScratchDir("evm");
    const auto signal = Generate(QamConfig());
    ExportSignal(dir / "tx.sigmf-data", signal, ExportFormat::SigMF);
    const auto report = AnalyzeRecording(ReadRecording(dir / "tx.sigmf-meta"));
    const auto expected = MeasureSymbolAccuracy(signal);
    ASSERT_TRUE(report.Accuracy.has_value());
    ASSERT_TRUE(expected.has_value());
    EXPECT_DOUBLE_EQ(report.Accuracy->EvmRms, expected->EvmRms);
    EXPECT_EQ(report.Waveform, std::string("16-QAM"));
    // Post-filter SNR = sample SNR + 10 log10(SPS); the measurement tracks the requested 20 dB.
    EXPECT_NEAR(report.SampleSnrDb, 20.0, 1.0);
    const auto stats = MeasurePowerStatistics(signal.Samples);
    EXPECT_DOUBLE_EQ(report.Power.PaprDb, stats.PaprDb);
    EXPECT_DOUBLE_EQ(report.SampleRateHz, signal.SampleRateHz);
}

TEST(Analyze, EvmReflectsSamplesChangedAfterExport) {
    const auto dir = ScratchDir("edited");
    auto config = QamConfig();
    config.Awgn.Enabled = false;
    auto signal = Generate(config);
    ExportSignal(dir / "tx.sigmf-data", signal, ExportFormat::SigMF);
    auto recording = ReadRecording(dir / "tx.sigmf-meta");
    const auto clean = AnalyzeRecording(recording);
    std::mt19937 rng(1);
    std::normal_distribution<float> gauss(0.0f, 0.1f);
    for (auto &s : recording.Samples) {
        s += std::complex<float>(gauss(rng), gauss(rng));
    }
    const auto noisy = AnalyzeRecording(recording);
    ASSERT_TRUE(clean.Accuracy && noisy.Accuracy);
    EXPECT_LT(clean.Accuracy->EvmRms, 0.02);
    EXPECT_GT(noisy.Accuracy->EvmRms, 5 * clean.Accuracy->EvmRms);
}

TEST(Analyze, SpectrumFindsToneAndBandwidth) {
    Recording recording;
    recording.SampleRateHz = 8000;
    for (int k = 0; k < 8192; ++k) {
        const double phase = 2 * M_PI * 1000.0 * k / 8000;
        recording.Samples.emplace_back(static_cast<float>(std::cos(phase)), static_cast<float>(std::sin(phase)));
    }
    const auto report = AnalyzeRecording(recording);
    EXPECT_NEAR(report.PeakFrequencyHz, 1000, 8000.0 / 1024);
    EXPECT_NEAR(report.Power.MeanPower, 1, 1e-6);
    EXPECT_NEAR(report.Power.PaprDb, 0, 1e-6);
    EXPECT_LT(report.OccupiedBandwidthHz, 100);
    EXPECT_FALSE(report.Accuracy.has_value());
    EXPECT_NE(ReportText(report).find("PAPR"), std::string::npos);
    const auto json = nlohmann::json::parse(ReportJson(report));
    EXPECT_EQ(json.at("sample_count"), 8192);
}

TEST(Recording, ReadsSigmfCi16WithScaling) {
    const auto dir = ScratchDir("ci16");
    WriteFile(
        dir / "r.sigmf-meta",
        R"({"global": {"core:datatype": "ci16_le", "core:sample_rate": 1000.0, "core:version": "1.0.0"}, "captures": [], "annotations": []})");
    // (16384, -32768) and (-1, 0) little-endian.
    WriteFile(dir / "r.sigmf-data", std::string("\x00\x40\x00\x80\xff\xff\x00\x00", 8));
    const auto recording = ReadRecording(dir / "r.sigmf-meta");
    ASSERT_EQ(recording.Samples.size(), 2u);
    EXPECT_FLOAT_EQ(recording.Samples[0].real(), 0.5f);
    EXPECT_FLOAT_EQ(recording.Samples[0].imag(), -1.0f);
    EXPECT_FLOAT_EQ(recording.Samples[1].real(), -1.0f / 32768);
}

TEST(Recording, RejectsBadInputs) {
    const auto dir = ScratchDir("bad");
    WriteFile(dir / "u.sigmf-meta", R"({"global": {"core:datatype": "cu8", "core:sample_rate": 1000}})");
    WriteFile(dir / "u.sigmf-data", "abcd");
    EXPECT_THROW(ReadRecording(dir / "u.sigmf-meta"), std::runtime_error);
    WriteFile(dir / "t.sigmf-meta", R"({"global": {"core:datatype": "cf32_le", "core:sample_rate": 1000}})");
    WriteFile(dir / "t.sigmf-data", std::string(10, 'x'));
    EXPECT_THROW(ReadRecording(dir / "t.sigmf-meta"), std::runtime_error);
    WriteFile(dir / "r.sigmf-meta", R"({"global": {"core:datatype": "cf32_le"}})");
    EXPECT_THROW(ReadRecording(dir / "r.sigmf-meta"), std::runtime_error);
    WriteFile(dir / "m.sigmf-meta", "not json");
    EXPECT_THROW(ReadRecording(dir / "m.sigmf-meta"), std::invalid_argument);
    EXPECT_THROW(ReadRecording(dir / "missing.sigmf-meta"), std::runtime_error);
    EXPECT_THROW(ReadRecording(dir / "tx.iq"), std::runtime_error);
    WriteFile(dir / "nan.sigmf-meta", R"({"global": {"core:datatype": "cf32_le", "core:sample_rate": 1000}})");
    WriteFile(dir / "nan.sigmf-data", std::string("\x00\x00\xc0\x7f\x00\x00\x00\x00", 8));
    EXPECT_THROW(ReadRecording(dir / "nan.sigmf-meta"), std::runtime_error);
    EXPECT_THROW(AnalyzeRecording(Recording{}), std::invalid_argument);
}

} // namespace Core
