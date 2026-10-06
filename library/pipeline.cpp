#include "pipeline.h"
#include <stdexcept>

namespace iq {
PipelineStages pipeline_stages(const GeneratedSignal& signal) {
    if (signal.family != Family::Linear) throw std::invalid_argument("Only linear waveforms have a pipeline view");
    PipelineStages stages;
    const auto& config = signal.config;
    const auto sps = static_cast<std::size_t>(config.samples_per_symbol);
    const auto q_delay = quadrature_delay_samples(config);
    stages.bits = signal.bits;
    stages.bits_per_symbol = bits_per_symbol(config.modulation);
    stages.samples_per_symbol = config.samples_per_symbol;
    stages.sample_rate_hz = signal.sample_rate_hz;
    stages.filter_delay_samples = signal.filter_delay_samples;
    stages.quadrature_delay_samples = q_delay;
    const auto gain = static_cast<float>(config.amplitude_gain);
    stages.symbols.reserve(signal.symbols.size());
    for (const auto& symbol : signal.symbols) stages.symbols.push_back(symbol * gain);
    stages.upsampled.assign(signal.symbols.size() * sps + q_delay, {});
    for (std::size_t k = 0; k < stages.symbols.size(); ++k) {
        stages.upsampled[k * sps] += std::complex<float>(stages.symbols[k].real(), 0);
        stages.upsampled[k * sps + q_delay] += std::complex<float>(0, stages.symbols[k].imag());
    }
    stages.received = signal.samples;
    stages.degraded = config.awgn.enabled || signal.impairments_applied;
    if (stages.degraded) {
        auto clean = config;
        clean.awgn.enabled = false;
        clean.impairments = {};
        stages.shaped = generate(clean).samples;
    } else
        stages.shaped = signal.samples;
    return stages;
}
}
