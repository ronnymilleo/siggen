/**
 * @file    signal_processing_test.cpp
 * @brief   Tests for the root-raised-cosine filter: validation, symmetry, energy and analytical limits.
 */

#include "signal_processing.h"

#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <numbers>
#include <numeric>

namespace Core {

TEST(RRC, Validation) {
    for (double roll_off :
         {-0.1, 1.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        EXPECT_THROW(RRCFilter(roll_off, 10, 8), std::invalid_argument);
    }
    EXPECT_THROW(RRCFilter(.2, 0, 8), std::invalid_argument);
    EXPECT_THROW(RRCFilter(.2, 10, 1), std::invalid_argument);
    EXPECT_THROW(RRCFilter(.2, 3, 3), std::invalid_argument);
    EXPECT_THROW(RRCFilter(.2, std::numeric_limits<int>::max(), 32), std::invalid_argument);
}

TEST(RRC, SymmetryEnergyAndNearbySingularities) {
    for (double roll_off : {0., .2, .25, .5, 1., std::nextafter(.25, 0.), std::nextafter(.25, 1.)}) {
        const auto taps = RRCFilter(roll_off, 10, 8);
        ASSERT_EQ(taps.size(), 81u);
        EXPECT_NEAR(std::inner_product(taps.begin(), taps.end(), taps.begin(), 0.), 1., 2e-15);
        for (std::size_t i = 0; i < taps.size(); ++i) {
            EXPECT_TRUE(std::isfinite(taps[i]));
            EXPECT_DOUBLE_EQ(taps[i], taps[taps.size() - 1 - i]);
        }
    }
    const auto taps = RRCFilter(.25, 10, 8);
    for (double roll_off : {std::nextafter(.25, 0.), std::nextafter(.25, 1.)}) {
        auto near = RRCFilter(roll_off, 10, 8);
        for (std::size_t i = 0; i < taps.size(); ++i) {
            EXPECT_NEAR(near[i], taps[i], 1e-15);
        }
    }
}

TEST(RRC, AnalyticalLimitsAndIndependentClosedFormFixture) {
    constexpr double Pi = std::numbers::pi;
    auto taps = RRCFilter(.25, 10, 8);
    const double center = 1 + .25 * (4 / Pi - 1);
    const double singular = .25 / std::sqrt(2.) * ((1 + 2 / Pi) * std::sin(Pi) + (1 - 2 / Pi) * std::cos(Pi));
    EXPECT_NEAR(taps[48] / taps[40], singular / center, 1e-15);
    taps = RRCFilter(0, 2, 2);
    // sinc sampled at {-1,-1/2,0,1/2,1}; normalized analytically.
    double norm = std::sqrt(1 + 8 / (Pi * Pi));
    const std::vector<double> fixture{0, 2 / Pi / norm, 1 / norm, 2 / Pi / norm, 0};
    for (std::size_t i = 0; i < taps.size(); ++i) {
        EXPECT_NEAR(taps[i], fixture[i], 1e-15);
    }
    taps = RRCFilter(1, 2, 2);
    // beta=1: h(t)=4*cos(2*pi*t)/(pi*(1-16*t*t)).
    norm = std::sqrt(2. / 225 + 2. / 9 + 1);
    const std::vector<double> beta_one{-1. / 15 / norm, 1. / 3 / norm, 1 / norm, 1. / 3 / norm, -1. / 15 / norm};
    for (std::size_t i = 0; i < taps.size(); ++i) {
        EXPECT_NEAR(taps[i], beta_one[i], 1e-15);
    }
}

TEST(RRC, QuarterSymbolAnalyticalLimitAndPerturbations) {
    constexpr double Pi = std::numbers::pi;
    const auto unit_roll_off = RRCFilter(1, 10, 4);
    EXPECT_NEAR(unit_roll_off[21] / unit_roll_off[20], Pi / 4, 1e-15);
    for (double roll_off : {.999999999, .500000001, .499999999, .200000001, .199999999}) {
        auto taps = RRCFilter(roll_off, 10, 8);
        for (double tap : taps) {
            EXPECT_TRUE(std::isfinite(tap));
        }
    }
}

} // namespace Core
