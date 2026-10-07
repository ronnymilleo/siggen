#pragma once
#include <cstddef>
#include <vector>

inline constexpr std::size_t MAX_SIGNAL_SAMPLES = 4 * 1024 * 1024;
std::vector<double> RRCFilter(double beta, int span, int SPS);
std::vector<double> Convolve(const std::vector<double>& signal, const std::vector<double>& filter);
std::vector<double> ApplyRRCFilter(const std::vector<double>& signal, double beta, int span, int SPS);
std::vector<float> GenerateConstellationDiagramPoints(int start, int end, int SPS, const std::vector<float>& data);
