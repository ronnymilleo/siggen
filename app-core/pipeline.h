/**
 * @file    pipeline.h
 * @brief   The intermediate stages of a linear signal, from data bits to transmitted samples.
 */

#ifndef SIGGEN_PIPELINE_H
#define SIGGEN_PIPELINE_H

#include "generator.h"
#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  PipelineStages
 * @brief   Every stage between the data bits and the transmitted samples of a linear signal.
 * @details All stages share one time base (stage index k * SPS) for the step-by-step view.
 */
struct PipelineStages {
    std::string Bits; // Transmitted bits
    int BitsPerSymbol = 0;
    int SamplesPerSymbol = 0;
    double SampleRateHz = 0;
    std::size_t FilterDelaySamples = 0;         // Group delay of the pulse filter
    std::size_t QuadratureDelaySamples = 0;     // OQPSK half-symbol lag of Q
    std::vector<std::complex<float>> Symbols;   // Mapped symbols with amplitude gain applied
    std::vector<std::complex<float>> Upsampled; // Zeros inserted: one impulse per symbol every SPS samples
    std::vector<std::complex<float>> Shaped;    // Pulse-filter output, before AWGN and impairments
    std::vector<std::complex<float>> Received;  // Final samples: shaped plus AWGN and impairments
    bool Degraded = false;                      // True when AWGN or impairments make received differ from shaped
};

PipelineStages BuildPipelineStages(const GeneratedSignal &signal);

} // namespace Core

#endif // SIGGEN_PIPELINE_H
