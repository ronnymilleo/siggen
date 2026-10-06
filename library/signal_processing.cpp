#include "signal_processing.h"
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
double sinc(double x) { return x == 0 ? 1 : std::sin(x) / x; }
}

std::vector<double> RRCFilter(double beta, int span, int SPS) {
    if (!std::isfinite(beta) || beta < 0 || beta > 1 || span < 1 || SPS < 2 ||
        static_cast<std::size_t>(span) > (MAX_SIGNAL_SAMPLES - 1) / static_cast<std::size_t>(SPS))
        throw std::invalid_argument("RRC requires beta in [0,1], positive span, SPS >= 2 and bounded tap count");
    const auto order = static_cast<std::size_t>(span) * static_cast<std::size_t>(SPS);
    if (order % 2 != 0) throw std::invalid_argument("RRC filter order (span * SPS) must be even");
    std::vector<double> taps(order + 1);
    constexpr double pi = std::numbers::pi;
    double energy = 0;
    for (std::size_t i = 0; i <= order; ++i) {
        const double t = std::abs((static_cast<double>(i) - static_cast<double>(order) / 2) / SPS);
        const double x = 4 * beta * t;
        double h;
        if (t == 0) h = 1 + beta * (4 / pi - 1);
        else if (beta == 0) h = sinc(pi * t);
        else if (std::abs(x - 1) < 0.25) {
            // Factor the removable zero analytically; no subtraction of nearly equal terms.
            const double d = x - 1;
            const double a = pi * t * (1 - beta);
            const double s = sinc(pi * d / 4);
            h = (std::sin(a) * (-1 + x * pi * pi * d / 8 * s * s)
                 - x * std::cos(a) * pi / 2 * sinc(pi * d / 2)) / (-pi * t * (x + 1));
        } else {
            h = (std::sin(pi * t * (1 - beta)) + x * std::cos(pi * t * (1 + beta))) /
                (pi * t * (1 - x * x));
        }
        taps[i] = h;
        energy += h * h;
    }
    for (auto& tap : taps) tap /= std::sqrt(energy);
    return taps;
}

std::vector<double> Convolve(const std::vector<double>& signal, const std::vector<double>& filter) {
    if (signal.empty() || filter.empty()) return {};
    if (filter.size() > MAX_SIGNAL_SAMPLES || signal.size() > MAX_SIGNAL_SAMPLES - filter.size() + 1)
        throw std::length_error("Convolution exceeds sample limit");
    for (const auto& input : {&signal, &filter})
        for (double x : *input)
            if (!std::isfinite(x)) throw std::invalid_argument("Convolution input must be finite");
    std::vector<double> output(signal.size() + filter.size() - 1, 0);
    for (std::size_t i = 0; i < signal.size(); ++i)
        for (std::size_t j = 0; j < filter.size(); ++j) output[i + j] += signal[i] * filter[j];
    for (double x : output)
        if (!std::isfinite(x)) throw std::overflow_error("Convolution output overflow");
    return output;
}

std::vector<double> ApplyRRCFilter(const std::vector<double>& signal, double beta, int span, int SPS) {
    return Convolve(signal, RRCFilter(beta, span, SPS));
}

std::vector<float> GenerateConstellationDiagramPoints(int start, int end, int SPS, const std::vector<float>& data) {
    if (start < 0 || end < start || static_cast<std::size_t>(end) > data.size() || SPS <= 0)
        throw std::invalid_argument("Invalid constellation sampling range or SPS");
    std::vector<float> points;
    for (auto i = static_cast<std::size_t>(start); i < static_cast<std::size_t>(end); i += static_cast<std::size_t>(SPS))
        points.push_back(data[i]);
    return points;
}
