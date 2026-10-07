#pragma once
#include "generator.h"
#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace iq {
// Every stage between the data bits and the transmitted samples of a linear
// signal, kept on one time base (stage index k * SPS) for the step-by-step view.
struct PipelineStages {
    std::string bits;                          // Transmitted bits.
    int bits_per_symbol = 0;
    int samples_per_symbol = 0;
    double sample_rate_hz = 0;
    std::size_t filter_delay_samples = 0;      // Group delay of the pulse filter.
    std::size_t quadrature_delay_samples = 0; // OQPSK half-symbol lag of Q.
    std::vector<std::complex<float>> symbols;   // Mapped symbols with amplitude gain applied.
    std::vector<std::complex<float>> upsampled; // Zeros inserted: one impulse per symbol every SPS samples.
    std::vector<std::complex<float>> shaped;    // Pulse-filter output, before AWGN and impairments.
    std::vector<std::complex<float>> received;  // Final samples: shaped plus AWGN and impairments.
    bool degraded = false;                      // True when AWGN or impairments make received differ from shaped.
};
// Throws std::invalid_argument for noise and FSK signals, which have no symbol/filter pipeline.
PipelineStages pipeline_stages(const GeneratedSignal& signal);
}
