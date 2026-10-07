#include "ber.h"
#include "demodulator.h"
#include "theory.h"
#include <cmath>
#include <gtest/gtest.h>

namespace {
iq::GenerationConfig config_for(iq::Modulation m) {
    iq::GenerationConfig c;
    iq::select_waveform(c, m);
    c.symbol_count = 512;
    return c;
}
double measured(iq::Modulation m, double eb_n0_db, std::size_t min_errors = 400) {
    iq::BerSweepSettings s;
    s.eb_n0_db = {eb_n0_db};
    s.min_errors = min_errors;
    s.max_bits = 3'000'000;
    const auto points = iq::ber_sweep(config_for(m), s);
    return points.at(0).ber();
}
}

TEST(Theory, QFunctionKnownValues) {
    EXPECT_NEAR(iq::q_function(0), 0.5, 1e-12);
    EXPECT_NEAR(iq::q_function(1), 0.158655254, 1e-8);
    EXPECT_NEAR(iq::q_function(3), 1.3498980e-3, 1e-9);
}

TEST(Theory, BpskAtTenDb) {
    EXPECT_NEAR(*iq::theoretical_ber(iq::Modulation::BPSK, 9.6), 1.0e-5, 2e-6);
    EXPECT_EQ(iq::theoretical_ber(iq::Modulation::BPSK, 0), iq::theoretical_ber(iq::Modulation::QPSK, 0));
}

TEST(Theory, UnavailableWhereNoClosedForm) {
    EXPECT_FALSE(iq::theoretical_ber(iq::Modulation::WGN, 5));
    EXPECT_FALSE(iq::theoretical_ber(iq::Modulation::FSK2, 5));
}

TEST(Theory, DecreasesWithEbN0) {
    for (auto m : {iq::Modulation::BPSK, iq::Modulation::PSK8, iq::Modulation::QAM16, iq::Modulation::DBPSK,
                   iq::Modulation::DQPSK, iq::Modulation::PI4DQPSK, iq::Modulation::PAM4, iq::Modulation::OOK, iq::Modulation::ASK4}) {
        double previous = 1;
        for (double db = 0; db <= 12; db += 2) {
            const double ber = *iq::theoretical_ber(m, db);
            EXPECT_LT(ber, previous) << static_cast<int>(m) << " at " << db;
            previous = ber;
        }
    }
}

TEST(Theory, DifferentialPenaltyOverCoherent) {
    EXPECT_GT(*iq::theoretical_ber(iq::Modulation::DBPSK, 6), *iq::theoretical_ber(iq::Modulation::BPSK, 6));
    EXPECT_GT(*iq::theoretical_ber(iq::Modulation::DQPSK, 6), *iq::theoretical_ber(iq::Modulation::QPSK, 6));
}

TEST(Demodulator, NoiselessLinkIsErrorFreeForEveryLinearWaveform) {
    for (int i = 0; i <= static_cast<int>(iq::Modulation::ASK4); ++i) {
        const auto m = static_cast<iq::Modulation>(i);
        if (!iq::is_valid(m) || !iq::has_reference_demodulator(m)) continue;
        const auto errors = iq::bit_errors(iq::generate(config_for(m)));
        ASSERT_TRUE(errors) << i;
        EXPECT_GT(errors->bit_count, 0u);
        EXPECT_EQ(errors->bit_errors, 0u) << i;
    }
}

TEST(Demodulator, NotAvailableForNoiseOrFsk) {
    EXPECT_FALSE(iq::has_reference_demodulator(iq::Modulation::WGN));
    EXPECT_FALSE(iq::has_reference_demodulator(iq::Modulation::FSK2));
    EXPECT_FALSE(iq::has_reference_demodulator(iq::Modulation::MSK));
}

TEST(Demodulator, SnrConversion) {
    auto c = config_for(iq::Modulation::QPSK);
    c.samples_per_symbol = 8;
    EXPECT_NEAR(iq::snr_for_eb_n0(c, 10), 10 + 10 * std::log10(2.0 / 8.0), 1e-12);
}

TEST(Ber, MatchesTheory) {
    struct Case { iq::Modulation m; double db; double tolerance; };
    for (const auto& c : {Case{iq::Modulation::BPSK, 4, 0.2}, Case{iq::Modulation::QPSK, 4, 0.2},
                          Case{iq::Modulation::QAM16, 8, 0.2}, Case{iq::Modulation::PSK8, 9, 0.25},
                          Case{iq::Modulation::DBPSK, 6, 0.2}, Case{iq::Modulation::DQPSK, 8, 0.2},
                          Case{iq::Modulation::OOK, 8, 0.2}, Case{iq::Modulation::PAM4, 8, 0.2}}) {
        const double theory = *iq::theoretical_ber(c.m, c.db);
        const double ber = measured(c.m, c.db);
        EXPECT_NEAR(ber / theory, 1.0, c.tolerance) << static_cast<int>(c.m) << " measured " << ber << " theory " << theory;
    }
}

TEST(Ber, SweepIsReproducible) {
    iq::BerSweepSettings s;
    s.eb_n0_db = {2, 4};
    s.min_errors = 50;
    s.max_bits = 100000;
    const auto a = iq::ber_sweep(config_for(iq::Modulation::QPSK), s);
    const auto b = iq::ber_sweep(config_for(iq::Modulation::QPSK), s);
    ASSERT_EQ(a.size(), 2u);
    EXPECT_EQ(a[0].errors.bit_errors, b[0].errors.bit_errors);
    EXPECT_GT(a[0].ber(), a[1].ber());
}

TEST(Ber, RejectsWaveformsWithoutReceiver) {
    iq::BerSweepSettings s;
    s.eb_n0_db = {5};
    EXPECT_THROW(iq::ber_sweep(config_for(iq::Modulation::FSK2), s), std::invalid_argument);
}

TEST(Ber, CarrierOffsetBreaksCoherentButNotDifferential) {
    auto run = [](iq::Modulation m) {
        auto c = config_for(m);
        c.impairments.cfo_hz = 20;
        iq::BerSweepSettings s;
        s.eb_n0_db = {30};
        s.max_bits = 40000;
        return iq::ber_sweep(c, s).at(0).ber();
    };
    EXPECT_GT(run(iq::Modulation::QPSK), 0.1);
    EXPECT_LT(run(iq::Modulation::DQPSK), 0.01);
}

#include "analysis.h"
#include "iq_export.h"
#include "batch.h"
#include "recording.h"
#include <filesystem>

TEST(Analyze, ReportsBitErrorsAndTheoryFromARecording) {
    const auto dir = std::filesystem::temp_directory_path() / "siggen-ber-analyze";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto c = config_for(iq::Modulation::QPSK);
    c.symbol_count = 20000;
    c.awgn.enabled = true;
    c.awgn.snr_db = 1; // Eb/N0 = 1 + 10 log10(8/2) = 7 dB.
    iq::export_signal(dir / "tx.sigmf-data", iq::generate(c), iq::ExportFormat::SigMF);
    const auto report = iq::analyze_recording(iq::read_recording(dir / "tx.sigmf-meta"));
    ASSERT_TRUE(report.errors.has_value());
    ASSERT_TRUE(report.theoretical_ber.has_value());
    EXPECT_NEAR(report.measured_eb_n0_db, 7, 0.3);
    EXPECT_NEAR(report.errors->ber() / *report.theoretical_ber, 1.0, 0.25);
    EXPECT_NE(iq::report_text(report).find("BER"), std::string::npos);
    EXPECT_NE(iq::report_json(report).find("\"bit_errors\""), std::string::npos);
}

TEST(Analyze, BatchFrameScoresBitsOnItsInterior) {
    const auto dir = std::filesystem::temp_directory_path() / "siggen-ber-frames";
    std::filesystem::remove_all(dir);
    iq::BatchRequest request;
    request.base = config_for(iq::Modulation::QPSK);
    request.snrs_db = {-1};
    request.frame_size = 2048;
    request.output_dir = dir;
    iq::run_batch(request);
    std::filesystem::path meta;
    for (const auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().extension() == ".sigmf-meta") meta = e.path();
    const auto report = iq::analyze_recording(iq::read_recording(meta));
    ASSERT_TRUE(report.errors.has_value());
    EXPECT_GT(report.errors->bit_count, 100u);
    EXPECT_LT(report.errors->bit_count, 2048u);
    EXPECT_GT(report.errors->bit_errors, 0u);
}
