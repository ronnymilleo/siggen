#include "siggen_c.h"
#include "generator.h"
#include "iq_export.h"
#include "measurements.h"
#include "preset.h"
#include "recording.h"
#include "signal_analysis.h"
#include <algorithm>
#include <cstring>
#include <complex>
#include <exception>
#include <stdexcept>
#include <vector>
#include <string>

struct siggen_signal {
    iq::GeneratedSignal signal;
    bool has_config = false;
    std::size_t expected_samples = 0; // Length implied by the configuration.
};

namespace {
void set_error(char* error, std::size_t capacity, const std::string& message) {
    if (!error || capacity == 0) return;
    std::strncpy(error, message.c_str(), capacity - 1);
    error[capacity - 1] = '\0';
}
long copy_text(const std::string& text, char* out, std::size_t capacity) {
    if (out && capacity > 0) {
        std::strncpy(out, text.c_str(), capacity - 1);
        out[capacity - 1] = '\0';
    }
    return static_cast<long>(text.size());
}
template <class F>
auto guarded(char* error, std::size_t capacity, decltype(std::declval<F>()()) failure, F&& body) {
    try {
        return body();
    } catch (const std::exception& e) {
        set_error(error, capacity, e.what());
    } catch (...) {
        set_error(error, capacity, "unknown error");
    }
    return failure;
}
}

extern "C" {
const char* siggen_version(void) { return SIGGEN_VERSION; }

int siggen_default_preset(char* out, std::size_t capacity) {
    return static_cast<int>(copy_text(iq::serialize_preset(iq::GenerationConfig{}), out, capacity));
}

siggen_signal* siggen_generate(const char* preset_text, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, static_cast<siggen_signal*>(nullptr), [&] {
        auto handle = new siggen_signal;
        try {
            handle->signal = iq::generate(iq::parse_preset(preset_text ? preset_text : ""));
        } catch (...) {
            delete handle;
            throw;
        }
        handle->has_config = true;
        handle->expected_samples = handle->signal.samples.size();
        return handle;
    });
}

siggen_signal* siggen_load(const char* path, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, static_cast<siggen_signal*>(nullptr), [&] {
        const auto recording = iq::read_recording(path ? path : "");
        auto handle = new siggen_signal;
        if (auto signal = iq::signal_from_recording(recording)) {
            handle->signal = std::move(*signal);
            handle->has_config = true;
        } else {
            handle->signal.samples = recording.samples;
            handle->signal.sample_rate_hz = recording.sample_rate_hz;
            if (recording.config) handle->signal.config = *recording.config;
        }
        handle->signal.sample_rate_hz = recording.sample_rate_hz;
        handle->expected_samples = handle->signal.samples.size();
        return handle;
    });
}

void siggen_free(siggen_signal* signal) { delete signal; }
std::size_t siggen_sample_count(const siggen_signal* s) { return s->signal.samples.size(); }
double siggen_sample_rate(const siggen_signal* s) { return s->signal.sample_rate_hz; }
std::size_t siggen_filter_delay(const siggen_signal* s) { return s->signal.filter_delay_samples; }

void siggen_copy_samples(const siggen_signal* s, float* out) {
    for (const auto& v : s->signal.samples) { *out++ = v.real(); *out++ = v.imag(); }
}

int siggen_set_samples(siggen_signal* s, const float* interleaved, std::size_t n, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, -1, [&] {
        if (!interleaved && n) throw std::invalid_argument("samples are null");
        std::vector<std::complex<float>> samples(n);
        for (std::size_t k = 0; k < n; ++k) samples[k] = {interleaved[2 * k], interleaved[2 * k + 1]};
        s->signal.samples = std::move(samples);
        return 0;
    });
}

std::size_t siggen_symbol_count(const siggen_signal* s) { return s->signal.symbols.size(); }
void siggen_copy_symbols(const siggen_signal* s, float* out) {
    for (const auto& v : s->signal.symbols) { *out++ = v.real(); *out++ = v.imag(); }
}

long siggen_preset_text(const siggen_signal* s, char* out, std::size_t capacity) {
    if (!s->has_config) return -1;
    return copy_text(iq::serialize_preset(s->signal.config), out, capacity);
}
long siggen_waveform_name(const siggen_signal* s, char* out, std::size_t capacity) {
    if (!s->has_config) return -1;
    return copy_text(iq::modulation_name(s->signal.config.modulation), out, capacity);
}
long siggen_bits(const siggen_signal* s, char* out, std::size_t capacity) { return copy_text(s->signal.bits, out, capacity); }

int siggen_power_statistics(const siggen_signal* s, double* out) {
    if (s->signal.samples.empty()) return -1;
    const auto stats = iq::power_statistics(s->signal.samples);
    out[0] = stats.mean_power; out[1] = stats.peak_power; out[2] = stats.papr_db;
    return 0;
}

int siggen_symbol_accuracy(const siggen_signal* s, double* out, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, -1, [&] {
        if (!s->has_config) return 0;
        if (s->signal.samples.size() != s->expected_samples)
            throw std::invalid_argument("The sample count changed from the generated length; EVM needs the original timing");
        const auto accuracy = iq::symbol_accuracy(s->signal);
        if (!accuracy) return 0;
        out[0] = static_cast<double>(accuracy->symbol_count); out[1] = accuracy->evm_rms; out[2] = accuracy->evm_db;
        out[3] = accuracy->snr_after_matched_db; out[4] = accuracy->expected_offset_db;
        return 1;
    });
}

long siggen_eye(const siggen_signal* s, std::size_t max_traces, double* time, std::size_t time_capacity, double* i, double* q,
                std::size_t trace_capacity, std::size_t* points) {
    return guarded(nullptr, 0, -1L, [&] {
        if (!s->has_config || s->signal.samples.size() != s->expected_samples) return -1L;
        const auto eye = iq::eye_diagram(s->signal, max_traces);
        const std::size_t count = eye.in_phase.size();
        const std::size_t width = eye.time_symbols.size();
        if (points) *points = width;
        if (time && time_capacity >= width) std::copy(eye.time_symbols.begin(), eye.time_symbols.end(), time);
        if (i && q && trace_capacity >= count * width)
            for (std::size_t k = 0; k < count; ++k) {
                std::copy(eye.in_phase[k].begin(), eye.in_phase[k].end(), i + k * width);
                std::copy(eye.quadrature[k].begin(), eye.quadrature[k].end(), q + k * width);
            }
        return static_cast<long>(count);
    });
}

long siggen_matched_symbols(const siggen_signal* s, float* out, std::size_t* symbol_indices, std::size_t capacity) {
    return guarded(nullptr, 0, -1L, [&] {
        if (!s->has_config || s->signal.samples.size() != s->expected_samples || s->signal.symbols.empty()) return -1L;
        const auto observations = iq::matched_symbols(s->signal);
        const auto count = observations.values.size();
        if (out && symbol_indices && capacity >= count)
            for (std::size_t k = 0; k < count; ++k) {
                out[2 * k] = observations.values[k].real();
                out[2 * k + 1] = observations.values[k].imag();
                symbol_indices[k] = observations.symbol_indices[k];
            }
        return static_cast<long>(count);
    });
}

long siggen_psd(const float* interleaved, std::size_t n, double sample_rate_hz, std::size_t segment_length, int window,
                double* frequency_hz, double* density, std::size_t capacity, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, -1L, [&] {
        if (window < 0 || window > 3) throw std::invalid_argument("window must be 0..3");
        std::vector<std::complex<float>> samples(n);
        for (std::size_t k = 0; k < n; ++k) samples[k] = {interleaved[2 * k], interleaved[2 * k + 1]};
        const auto psd = iq::welch_psd(samples, sample_rate_hz, segment_length, static_cast<iq::Window>(window));
        const auto bins = psd.frequency_hz.size();
        if (frequency_hz && density && capacity >= bins) {
            std::copy(psd.frequency_hz.begin(), psd.frequency_hz.end(), frequency_hz);
            std::copy(psd.power_density.begin(), psd.power_density.end(), density);
        }
        return static_cast<long>(bins);
    });
}

int siggen_export(const siggen_signal* s, const char* path, int format, int overwrite, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, -1, [&] {
        if (!s->has_config) throw std::invalid_argument("Only signals generated by siggen can be exported");
        if (format < 0 || format > 1) throw std::invalid_argument("format must be 0 (csv) or 1 (sigmf)");
        const auto kind = format == 0 ? iq::ExportFormat::CSV : iq::ExportFormat::SigMF;
        iq::export_signal(path ? path : "", s->signal, kind, overwrite != 0);
        return 0;
    });
}
}
