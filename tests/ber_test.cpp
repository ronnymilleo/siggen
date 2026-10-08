/**
 * @file    ber_test.cpp
 * @brief   Tests for the textbook BER curves, the reference demodulator and the BER sweep.
 */

#include "ber.h"

#include "analysis.h"
#include "batch.h"
#include "demodulator.h"
#include "iq_export.h"
#include "recording.h"
#include "theory.h"
#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>

namespace Core {

namespace {

GenerationConfig ConfigFor(Modulation m) {
    GenerationConfig c;
    SelectWaveform(c, m);
    c.SymbolCount = 512;
    return c;
}

double Measured(Modulation m, double eb_n0_db, std::size_t min_errors = 400) {
    BerSweepSettings s;
    s.EbN0Db = {eb_n0_db};
    s.MinErrors = min_errors;
    s.MaxBits = 3'000'000;
    const auto points = BerSweep(ConfigFor(m), s);
    return points.at(0).Ber();
}

} // namespace

TEST(Theory, QFunctionKnownValues) {
    EXPECT_NEAR(QFunction(0), 0.5, 1e-12);
    EXPECT_NEAR(QFunction(1), 0.158655254, 1e-8);
    EXPECT_NEAR(QFunction(3), 1.3498980e-3, 1e-9);
}

TEST(Theory, BpskAtTenDb) {
    EXPECT_NEAR(*TheoreticalBer(Modulation::BPSK, 9.6), 1.0e-5, 2e-6);
    EXPECT_EQ(TheoreticalBer(Modulation::BPSK, 0), TheoreticalBer(Modulation::QPSK, 0));
}

TEST(Theory, UnavailableWhereNoClosedForm) {
    EXPECT_FALSE(TheoreticalBer(Modulation::WGN, 5));
    EXPECT_FALSE(TheoreticalBer(Modulation::FSK2, 5));
}

TEST(Theory, DecreasesWithEbN0) {
    for (auto m : {Modulation::BPSK, Modulation::PSK8, Modulation::QAM16, Modulation::DBPSK, Modulation::DQPSK,
                   Modulation::PI4DQPSK, Modulation::PAM4, Modulation::OOK, Modulation::ASK4}) {
        double previous = 1;
        for (double db = 0; db <= 12; db += 2) {
            const double ber = *TheoreticalBer(m, db);
            EXPECT_LT(ber, previous) << static_cast<int>(m) << " at " << db;
            previous = ber;
        }
    }
}

TEST(Theory, DifferentialPenaltyOverCoherent) {
    EXPECT_GT(*TheoreticalBer(Modulation::DBPSK, 6), *TheoreticalBer(Modulation::BPSK, 6));
    EXPECT_GT(*TheoreticalBer(Modulation::DQPSK, 6), *TheoreticalBer(Modulation::QPSK, 6));
}

TEST(Demodulator, NoiselessLinkIsErrorFreeForEveryLinearWaveform) {
    for (int i = 0; i <= static_cast<int>(Modulation::ASK4); ++i) {
        const auto m = static_cast<Modulation>(i);
        if (!IsValid(m) || !HasReferenceDemodulator(m)) {
            continue;
        }
        const auto errors = CountBitErrors(Generate(ConfigFor(m)));
        ASSERT_TRUE(errors) << i;
        EXPECT_GT(errors->BitCount, 0u);
        EXPECT_EQ(errors->BitErrorCount, 0u) << i;
    }
}

TEST(Demodulator, NotAvailableForNoiseOrFsk) {
    EXPECT_FALSE(HasReferenceDemodulator(Modulation::WGN));
    EXPECT_FALSE(HasReferenceDemodulator(Modulation::FSK2));
    EXPECT_FALSE(HasReferenceDemodulator(Modulation::MSK));
}

TEST(Demodulator, SnrConversion) {
    auto c = ConfigFor(Modulation::QPSK);
    c.SamplesPerSymbol = 8;
    EXPECT_NEAR(SnrForEbN0(c, 10), 10 + 10 * std::log10(2.0 / 8.0), 1e-12);
}

TEST(Ber, MatchesTheory) {
    struct Case {
        Modulation M;
        double Db;
        double Tolerance;
    };
    for (const auto &c :
         {Case{Modulation::BPSK, 4, 0.2}, Case{Modulation::QPSK, 4, 0.2}, Case{Modulation::QAM16, 8, 0.2},
          Case{Modulation::PSK8, 9, 0.25}, Case{Modulation::DBPSK, 6, 0.2}, Case{Modulation::DQPSK, 8, 0.2},
          Case{Modulation::OOK, 8, 0.2}, Case{Modulation::PAM4, 8, 0.2}}) {
        const double theory = *TheoreticalBer(c.M, c.Db);
        const double ber = Measured(c.M, c.Db);
        EXPECT_NEAR(ber / theory, 1.0, c.Tolerance)
            << static_cast<int>(c.M) << " measured " << ber << " theory " << theory;
    }
}

TEST(Ber, SweepIsReproducible) {
    BerSweepSettings s;
    s.EbN0Db = {2, 4};
    s.MinErrors = 50;
    s.MaxBits = 100000;
    const auto a = BerSweep(ConfigFor(Modulation::QPSK), s);
    const auto b = BerSweep(ConfigFor(Modulation::QPSK), s);
    ASSERT_EQ(a.size(), 2u);
    EXPECT_EQ(a[0].Errors.BitErrorCount, b[0].Errors.BitErrorCount);
    EXPECT_GT(a[0].Ber(), a[1].Ber());
}

TEST(Ber, RejectsWaveformsWithoutReceiver) {
    BerSweepSettings s;
    s.EbN0Db = {5};
    EXPECT_THROW(BerSweep(ConfigFor(Modulation::FSK2), s), std::invalid_argument);
}

TEST(Ber, CarrierOffsetBreaksCoherentButNotDifferential) {
    auto run = [](Modulation m) {
        auto c = ConfigFor(m);
        c.Impairments.CfoHz = 20;
        BerSweepSettings s;
        s.EbN0Db = {30};
        s.MaxBits = 40000;
        return BerSweep(c, s).at(0).Ber();
    };
    EXPECT_GT(run(Modulation::QPSK), 0.1);
    EXPECT_LT(run(Modulation::DQPSK), 0.01);
}

TEST(Analyze, ReportsBitErrorsAndTheoryFromARecording) {
    const auto dir = std::filesystem::temp_directory_path() / "siggen-ber-analyze";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto c = ConfigFor(Modulation::QPSK);
    c.SymbolCount = 20000;
    c.Awgn.Enabled = true;
    c.Awgn.SnrDb = 1; // Eb/N0 = 1 + 10 log10(8/2) = 7 dB.
    ExportSignal(dir / "tx.sigmf-data", Generate(c), ExportFormat::SigMF);
    const auto report = AnalyzeRecording(ReadRecording(dir / "tx.sigmf-meta"));
    ASSERT_TRUE(report.Errors.has_value());
    ASSERT_TRUE(report.TheoreticalBer.has_value());
    EXPECT_NEAR(report.MeasuredEbN0Db, 7, 0.3);
    EXPECT_NEAR(report.Errors->Ber() / *report.TheoreticalBer, 1.0, 0.25);
    EXPECT_NE(ReportText(report).find("BER"), std::string::npos);
    EXPECT_NE(ReportJson(report).find("\"bit_errors\""), std::string::npos);
}

TEST(Analyze, BatchFrameScoresBitsOnItsInterior) {
    const auto dir = std::filesystem::temp_directory_path() / "siggen-ber-frames";
    std::filesystem::remove_all(dir);
    BatchRequest request;
    request.Base = ConfigFor(Modulation::QPSK);
    request.SnrsDb = {-1};
    request.FrameSize = 2048;
    request.OutputDir = dir;
    RunBatch(request);
    std::filesystem::path meta;
    for (const auto &e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() == ".sigmf-meta") {
            meta = e.path();
        }
    }
    const auto report = AnalyzeRecording(ReadRecording(meta));
    ASSERT_TRUE(report.Errors.has_value());
    EXPECT_GT(report.Errors->BitCount, 100u);
    EXPECT_LT(report.Errors->BitCount, 2048u);
    EXPECT_GT(report.Errors->BitErrorCount, 0u);
}

} // namespace Core
