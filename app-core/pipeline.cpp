/**
 * @file    pipeline.cpp
 * @brief   The intermediate stages of a linear signal, from data bits to transmitted samples.
 */

#include "pipeline.h"

#include <stdexcept>

namespace Core {

/**
 * @brief   Rebuilds the intermediate stages of a generated linear signal.
 * @param[in] signal  A generated linear signal.
 * @return  The bits, scaled symbols, upsampled impulses, shaped and received samples.
 * @note    When AWGN or impairments were applied, the shaped stage is generated again without them. Throws
 *          std::invalid_argument for noise and FSK signals, which have no symbol/filter pipeline.
 */
PipelineStages BuildPipelineStages(const GeneratedSignal &signal) {
    if (signal.Family != Family::Linear) {
        throw std::invalid_argument("Only linear waveforms have a pipeline view");
    }
    PipelineStages stages;
    const auto &config = signal.Config;
    const auto sps = static_cast<std::size_t>(config.SamplesPerSymbol);
    const auto quadrature_delay = QuadratureDelaySamples(config);
    stages.Bits = signal.Bits;
    stages.BitsPerSymbol = BitsPerSymbol(config.Modulation);
    stages.SamplesPerSymbol = config.SamplesPerSymbol;
    stages.SampleRateHz = signal.SampleRateHz;
    stages.FilterDelaySamples = signal.FilterDelaySamples;
    stages.QuadratureDelaySamples = quadrature_delay;
    const auto gain = static_cast<float>(config.AmplitudeGain);
    stages.Symbols.reserve(signal.Symbols.size());
    for (const auto &symbol : signal.Symbols) {
        stages.Symbols.push_back(symbol * gain);
    }
    stages.Upsampled.assign(signal.Symbols.size() * sps + quadrature_delay, {});
    for (std::size_t k = 0; k < stages.Symbols.size(); ++k) {
        stages.Upsampled[k * sps] += std::complex<float>(stages.Symbols[k].real(), 0);
        stages.Upsampled[k * sps + quadrature_delay] += std::complex<float>(0, stages.Symbols[k].imag());
    }
    stages.Received = signal.Samples;
    stages.Degraded = config.Awgn.Enabled || signal.ImpairmentsApplied;
    if (stages.Degraded) {
        auto clean = config;
        clean.Awgn.Enabled = false;
        clean.Impairments = {};
        stages.Shaped = Generate(clean).Samples;
    } else {
        stages.Shaped = signal.Samples;
    }
    return stages;
}

} // namespace Core
