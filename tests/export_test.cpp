#include "iq_export.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <span>
#include <sstream>
#include <fstream>
#include <chrono>
namespace {
iq::GeneratedSignal fixture() {
    iq::GenerationConfig c; c.symbol_count = 2; c.samples_per_symbol = 1;
    c.pulse = iq::Pulse::Rectangular; c.data_source = iq::DataSource::Explicit; c.bits = "01";
    return iq::generate(c);
}
}
TEST(Export, CSVPrecisionCountAndMetadata) {
    iq::GenerationConfig c; c.modulation = iq::Modulation::QPSK;
    const auto r = iq::generate(c);
    std::ostringstream out; iq::write_samples(out,r,iq::ExportFormat::CSV);
    std::istringstream input(out.str()); std::string line; std::getline(input,line);
    EXPECT_EQ(line,"time_s,i,q");
    std::size_t k = 0;
    while (std::getline(input,line)) {
        std::replace(line.begin(),line.end(),',',' ');
        std::istringstream row(line); double time; float i,q;
        ASSERT_TRUE(static_cast<bool>(row >> time >> i >> q));
        ASSERT_LT(k,r.samples.size());
        EXPECT_DOUBLE_EQ(time,k/r.sample_rate_hz);
        EXPECT_EQ(std::complex<float>(i,q), r.samples[k++]);
    }
    EXPECT_EQ(k,r.samples.size());
    auto meta = iq::export_metadata(r,iq::ExportFormat::CSV);
    EXPECT_NE(meta.find("\"sample_count\": " + std::to_string(k)), std::string::npos);
    EXPECT_NE(meta.find("\"sample_rate_hz\": 8000"), std::string::npos);
    EXPECT_NE(meta.find("\"filter_delay_samples\": 40"), std::string::npos);
    EXPECT_NE(meta.find("\"modulation\": \"QPSK\""), std::string::npos);
}
TEST(Export, BinaryByteOrderAndWriteFailure) {
    auto r = fixture(); std::ostringstream out;
    iq::write_samples(out,r,iq::ExportFormat::SigMF);
    const unsigned char bytes[] = {0,0,128,63,0,0,0,0,0,0,128,191,0,0,0,0};
    EXPECT_EQ(out.str(), std::string(reinterpret_cast<const char*>(bytes),sizeof bytes));
    std::ostringstream bad; bad.setstate(std::ios::badbit);
    EXPECT_THROW(iq::write_samples(bad,r,iq::ExportFormat::CSV), std::runtime_error);
    r.sample_rate_hz = 1;
    EXPECT_THROW(iq::export_metadata(r,iq::ExportFormat::CSV), std::invalid_argument);
}
TEST(Export, FilePairAndOverwriteProtection) {
    auto dir = std::filesystem::temp_directory_path() / ("iq-export-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    const auto path = dir / "test.sigmf-data";
    const auto r = fixture();
    iq::export_signal(path,r,iq::ExportFormat::SigMF);
    EXPECT_EQ(std::filesystem::file_size(path),16u);
    EXPECT_TRUE(std::filesystem::exists(iq::metadata_path(path)));
    EXPECT_THROW(iq::export_signal(path,r,iq::ExportFormat::CSV), std::runtime_error);
    EXPECT_EQ(std::filesystem::file_size(path),16u);
    EXPECT_NO_THROW(iq::export_signal(path,r,iq::ExportFormat::CSV,true));
    EXPECT_THROW(iq::export_signal(dir / "missing" / "x",r,iq::ExportFormat::CSV), std::ios_base::failure);
    std::filesystem::remove_all(dir);
}
TEST(Export, FlushFailureAndSidecarProtection) {
    struct FailFlush : std::stringbuf { int sync() override { return -1; } } buffer;
    std::ostream failing(&buffer);
    EXPECT_THROW(iq::write_samples(failing,fixture(),iq::ExportFormat::CSV),std::runtime_error);
    const auto dir=std::filesystem::temp_directory_path()/("iq-sidecar-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    const auto path=dir/"signal";
    { std::ofstream existing(iq::metadata_path(path)); existing << "keep"; }
    EXPECT_THROW(iq::export_signal(path,fixture(),iq::ExportFormat::CSV),std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_EQ(std::filesystem::file_size(iq::metadata_path(path)),4u);
    std::filesystem::remove_all(dir);
}
TEST(Export, Version2LinearAndAwgnMetadata) {
    iq::GenerationConfig c; c.symbol_count = 64;
    const auto clean = iq::generate(c);
    const auto clean_meta = iq::export_metadata(clean, iq::ExportFormat::CSV);
    EXPECT_NE(clean_meta.find("\"version\": 2"), std::string::npos);
    EXPECT_NE(clean_meta.find("\"family\": \"linear\""), std::string::npos);
    EXPECT_NE(clean_meta.find("\"waveform\": \"BPSK\""), std::string::npos);
    EXPECT_NE(clean_meta.find("\"symbol_energy\": 1"), std::string::npos);
    EXPECT_EQ(clean_meta.find("\"awgn\""), std::string::npos);
    c.awgn.enabled = true; c.awgn.snr_db = 10;
    const auto noisy = iq::generate(c);
    const auto meta = iq::export_metadata(noisy, iq::ExportFormat::SigMF);
    EXPECT_NE(meta.find("\"awgn\""), std::string::npos);
    EXPECT_NE(meta.find("\"requested_snr_db\": 10"), std::string::npos);
    EXPECT_NE(meta.find("\"reference_interval\": {\"begin\": 80, \"end\": 512}"), std::string::npos);
    EXPECT_NE(meta.find("\"added_noise_power\""), std::string::npos);
    EXPECT_NE(meta.find("\"noise_seed\": 5490"), std::string::npos);
    // Byte layout is unchanged: little-endian float32 I/Q pairs.
    std::ostringstream out;
    iq::write_samples(out, noisy, iq::ExportFormat::SigMF);
    EXPECT_EQ(out.str().size(), noisy.samples.size() * 8);
}
TEST(Export, NoiseFamilyMetadataOmitsSymbolClaims) {
    iq::GenerationConfig c; c.modulation = iq::Modulation::WGN; c.noise_source.sample_count = 16;
    auto r = iq::generate(c);
    std::ostringstream out;
    iq::write_samples(out, r, iq::ExportFormat::CSV);
    const auto text = out.str();
    EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 17u); // header + 16 rows
    const auto meta = iq::export_metadata(r, iq::ExportFormat::CSV);
    EXPECT_NE(meta.find("\"version\": 2"), std::string::npos);
    EXPECT_NE(meta.find("\"family\": \"noise\""), std::string::npos);
    EXPECT_NE(meta.find("\"waveform\": \"WGN\""), std::string::npos);
    EXPECT_NE(meta.find("\"noise_source\""), std::string::npos);
    EXPECT_NE(meta.find("\"noise_seed\": 5490"), std::string::npos);
    EXPECT_EQ(meta.find("symbol_energy"), std::string::npos);
    EXPECT_EQ(meta.find("\"bits\""), std::string::npos);
    EXPECT_EQ(meta.find("random_bit_rule"), std::string::npos);
    // Length validation distinguishes noise results from linear trains.
    r.samples.pop_back();
    EXPECT_THROW(iq::export_metadata(r, iq::ExportFormat::CSV), std::invalid_argument);
    r.config.noise_source.sample_count = 15;
    EXPECT_NO_THROW(iq::export_metadata(r, iq::ExportFormat::CSV));
}
TEST(Export, RawSpanWriterMatchesSignalWriter) {
    const auto r = fixture();
    std::ostringstream direct, via_signal;
    iq::write_samples(direct, std::span<const std::complex<float>>(r.samples), r.sample_rate_hz,
                      iq::ExportFormat::SigMF);
    iq::write_samples(via_signal, r, iq::ExportFormat::SigMF);
    EXPECT_EQ(direct.str(), via_signal.str());
    std::ostringstream bad;
    EXPECT_THROW(iq::write_samples(bad, std::span<const std::complex<float>>(r.samples), 0,
                                   iq::ExportFormat::CSV),
                 std::invalid_argument);
}
