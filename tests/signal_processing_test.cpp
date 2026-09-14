#include "signal_processing.h"
#include <gtest/gtest.h>
TEST(Convolution, KnownPolynomial) {
    EXPECT_EQ(Convolve({1, 2, 3}, {2, -1}), (std::vector<double>{2, 3, 4, -3}));
}
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
TEST(Convolution, EmptyAndInvalid) {
    EXPECT_TRUE(Convolve({}, {}).empty());
    EXPECT_TRUE(Convolve({1}, {}).empty());
    EXPECT_THROW(Convolve({NAN}, {1}), std::invalid_argument);
    EXPECT_THROW(Convolve(std::vector<double>(MAX_SIGNAL_SAMPLES), {1, 1}), std::length_error);
    EXPECT_THROW(GenerateConstellationDiagramPoints(0, 1, 0, {1}), std::invalid_argument);
}
TEST(RRC, Validation) {
    for (double b : {-0.1, 1.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        EXPECT_THROW(RRCFilter(b, 10, 8), std::invalid_argument);
    EXPECT_THROW(RRCFilter(.2, 0, 8), std::invalid_argument);
    EXPECT_THROW(RRCFilter(.2, 10, 1), std::invalid_argument);
    EXPECT_THROW(RRCFilter(.2, 3, 3), std::invalid_argument);
    EXPECT_THROW(RRCFilter(.2, std::numeric_limits<int>::max(), 32), std::invalid_argument);
}
TEST(RRC, SymmetryEnergyAndNearbySingularities) {
    for (double b : {0., .2, .25, .5, 1., std::nextafter(.25, 0.), std::nextafter(.25, 1.)}) {
        const auto h = RRCFilter(b, 10, 8);
        ASSERT_EQ(h.size(), 81u);
        EXPECT_NEAR(std::inner_product(h.begin(), h.end(), h.begin(), 0.), 1., 2e-15);
        for (std::size_t i = 0; i < h.size(); ++i) {
            EXPECT_TRUE(std::isfinite(h[i]));
            EXPECT_DOUBLE_EQ(h[i], h[h.size() - 1 - i]);
        }
    }
    const auto h = RRCFilter(.25, 10, 8);
    for (double b : {std::nextafter(.25, 0.), std::nextafter(.25, 1.)}) {
        auto near = RRCFilter(b, 10, 8);
        for (std::size_t i = 0; i < h.size(); ++i) EXPECT_NEAR(near[i], h[i], 1e-15);
    }
}
TEST(RRC, AnalyticalLimitsAndIndependentClosedFormFixture) {
    constexpr double pi = std::numbers::pi;
    auto h = RRCFilter(.25, 10, 8);
    const double center = 1 + .25 * (4 / pi - 1);
    const double singular = .25 / std::sqrt(2.) * ((1 + 2 / pi) * std::sin(pi) + (1 - 2 / pi) * std::cos(pi));
    EXPECT_NEAR(h[48] / h[40], singular / center, 1e-15);
    h = RRCFilter(0, 2, 2);
    // sinc sampled at {-1,-1/2,0,1/2,1}; normalized analytically.
    double norm = std::sqrt(1 + 8 / (pi * pi));
    const std::vector<double> fixture{0, 2 / pi / norm, 1 / norm, 2 / pi / norm, 0};
    for (std::size_t i = 0; i < h.size(); ++i) EXPECT_NEAR(h[i], fixture[i], 1e-15);
    h = RRCFilter(1, 2, 2);
    // beta=1: h(t)=4*cos(2*pi*t)/(pi*(1-16*t*t)).
    norm = std::sqrt(2. / 225 + 2. / 9 + 1);
    const std::vector<double> betaOne{-1./15/norm, 1./3/norm, 1/norm, 1./3/norm, -1./15/norm};
    for (std::size_t i = 0; i < h.size(); ++i) EXPECT_NEAR(h[i], betaOne[i], 1e-15);
}
TEST(RRC, QuarterSymbolAnalyticalLimitAndPerturbations) {
    constexpr double pi=std::numbers::pi;
    const auto h=RRCFilter(1,10,4);
    EXPECT_NEAR(h[21]/h[20],pi/4,1e-15);
    for (double b : {.999999999, .500000001, .499999999, .200000001, .199999999}) {
        auto taps=RRCFilter(b,10,8);
        for (double tap:taps) EXPECT_TRUE(std::isfinite(tap));
    }
}
