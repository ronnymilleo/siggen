/**
 * @file    signal_processing.h
 * @brief   Pulse-shaping filter design and the sample limit shared by every signal buffer.
 */

#ifndef SIGGEN_SIGNAL_PROCESSING_H
#define SIGGEN_SIGNAL_PROCESSING_H

#include <cstddef>
#include <vector>

namespace Core {

inline constexpr std::size_t MaxSignalSamples = 4 * 1024 * 1024;

std::vector<double> RRCFilter(double beta, int span, int sps);

} // namespace Core

#endif // SIGGEN_SIGNAL_PROCESSING_H
