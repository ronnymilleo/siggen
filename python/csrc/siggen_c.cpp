#include "siggen_c.h"
#include "ber.h"
#include "demodulator.h"
#include "generator.h"
#include "theory.h"
#include "iq_export.h"
#include "measurements.h"
#include "preset.h"
#include "recording.h"
#include "signal_analysis.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <complex>
#include <exception>
#include <vector>
#include <string>

struct siggen_signal {
    Core::GeneratedSignal signal;
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
// C ABI firewall: siggen reports errors as values, but a stray std::bad_alloc from the standard library must
// never unwind into C frames
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
    const auto text = Core::SerializePreset(Core::GenerationConfig{});
    if (!text.has_value()) return -1;
    return static_cast<int>(copy_text(*text, out, capacity));
}

siggen_signal* siggen_generate(const char* preset_text, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, static_cast<siggen_signal*>(nullptr), [&] {
        const auto config = Core::ParsePreset(preset_text ? preset_text : "");
        if (!config.has_value()) {
            set_error(error, error_capacity, config.error());
            return static_cast<siggen_signal*>(nullptr);
        }
        auto signal = Core::Generate(*config);
        if (!signal.has_value()) {
            set_error(error, error_capacity, signal.error());
            return static_cast<siggen_signal*>(nullptr);
        }
        auto handle = new siggen_signal;
        handle->signal = std::move(*signal);
        handle->has_config = true;
        handle->expected_samples = handle->signal.Samples.size();
        return handle;
    });
}

siggen_signal* siggen_load(const char* path, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, static_cast<siggen_signal*>(nullptr), [&] {
        const auto recording = Core::ReadRecording(path ? path : "");
        if (!recording.has_value()) {
            set_error(error, error_capacity, recording.error());
            return static_cast<siggen_signal*>(nullptr);
        }
        auto handle = new siggen_signal;
        if (auto signal = Core::SignalFromRecording(*recording)) {
            handle->signal = std::move(*signal);
            handle->has_config = true;
        } else {
            handle->signal.Samples = recording->Samples;
            handle->signal.SampleRateHz = recording->SampleRateHz;
            if (recording->Config) handle->signal.Config = *recording->Config;
        }
        handle->signal.SampleRateHz = recording->SampleRateHz;
        handle->expected_samples = handle->signal.Samples.size();
        return handle;
    });
}

void siggen_free(siggen_signal* signal) { delete signal; }
std::size_t siggen_sample_count(const siggen_signal* s) { return s->signal.Samples.size(); }
double siggen_sample_rate(const siggen_signal* s) { return s->signal.SampleRateHz; }
std::size_t siggen_filter_delay(const siggen_signal* s) { return s->signal.FilterDelaySamples; }

void siggen_copy_samples(const siggen_signal* s, float* out) {
    for (const auto& v : s->signal.Samples) { *out++ = v.real(); *out++ = v.imag(); }
}

int siggen_set_samples(siggen_signal* s, const float* interleaved, std::size_t n, char* error, std::size_t error_capacity) {
    if (!interleaved && n) {
        set_error(error, error_capacity, "samples are null");
        return -1;
    }
    return guarded(error, error_capacity, -1, [&] {
        std::vector<std::complex<float>> samples(n);
        for (std::size_t k = 0; k < n; ++k) samples[k] = {interleaved[2 * k], interleaved[2 * k + 1]};
        s->signal.Samples = std::move(samples);
        return 0;
    });
}

std::size_t siggen_symbol_count(const siggen_signal* s) { return s->signal.Symbols.size(); }
void siggen_copy_symbols(const siggen_signal* s, float* out) {
    for (const auto& v : s->signal.Symbols) { *out++ = v.real(); *out++ = v.imag(); }
}

long siggen_preset_text(const siggen_signal* s, char* out, std::size_t capacity) {
    if (!s->has_config) return -1;
    const auto text = Core::SerializePreset(s->signal.Config);
    if (!text.has_value()) return -1;
    return copy_text(*text, out, capacity);
}
long siggen_waveform_name(const siggen_signal* s, char* out, std::size_t capacity) {
    if (!s->has_config) return -1;
    return copy_text(Core::ModulationName(s->signal.Config.Modulation), out, capacity);
}
long siggen_bits(const siggen_signal* s, char* out, std::size_t capacity) { return copy_text(s->signal.Bits, out, capacity); }

int siggen_power_statistics(const siggen_signal* s, double* out) {
    if (s->signal.Samples.empty()) return -1;
    const auto stats = Core::MeasurePowerStatistics(s->signal.Samples);
    if (!stats.has_value()) return -1;
    out[0] = stats->MeanPower; out[1] = stats->PeakPower; out[2] = stats->PaprDb;
    return 0;
}

int siggen_symbol_accuracy(const siggen_signal* s, double* out, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, -1, [&] {
        if (!s->has_config) return 0;
        if (s->signal.Samples.size() != s->expected_samples) {
            set_error(error, error_capacity,
                      "The sample count changed from the generated length; EVM needs the original timing");
            return -1;
        }
        const auto accuracy = Core::MeasureSymbolAccuracy(s->signal);
        if (!accuracy.has_value()) {
            set_error(error, error_capacity, accuracy.error());
            return -1;
        }
        if (!*accuracy) return 0;
        out[0] = static_cast<double>((*accuracy)->SymbolCount); out[1] = (*accuracy)->EvmRms; out[2] = (*accuracy)->EvmDb;
        out[3] = (*accuracy)->SnrAfterMatchedDb; out[4] = (*accuracy)->ExpectedOffsetDb;
        return 1;
    });
}

int siggen_bit_errors(const siggen_signal* s, double* out, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, -1, [&] {
        if (!s->has_config) return 0;
        if (s->signal.Samples.size() != s->expected_samples) {
            set_error(error, error_capacity,
                      "The sample count changed from the generated length; the receiver needs the original timing");
            return -1;
        }
        const auto errors = Core::CountBitErrors(s->signal);
        if (!errors.has_value()) {
            set_error(error, error_capacity, errors.error());
            return -1;
        }
        if (!*errors) return 0;
        out[0] = static_cast<double>((*errors)->SymbolCount); out[1] = static_cast<double>((*errors)->SymbolErrorCount);
        out[2] = static_cast<double>((*errors)->BitCount); out[3] = static_cast<double>((*errors)->BitErrorCount);
        return 1;
    });
}

int siggen_theoretical_ber(const char* waveform, double eb_n0_db, double* out, char* error, std::size_t error_capacity) {
    Core::Modulation modulation;
    if (!Core::ParseModulation(waveform ? waveform : "", modulation)) {
        set_error(error, error_capacity, std::string("Unknown waveform: ") + (waveform ? waveform : ""));
        return -1;
    }
    const auto ber = Core::TheoreticalBer(modulation, eb_n0_db);
    if (!ber) return 0;
    *out = *ber;
    return 1;
}

long siggen_ber_curve(const char* preset_text, const double* eb_n0_db, std::size_t count, std::size_t min_errors, std::size_t max_bits,
                      int block_symbols, double* out, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, -1L, [&] {
        const auto config = Core::ParsePreset(preset_text ? preset_text : "");
        if (!config.has_value()) {
            set_error(error, error_capacity, config.error());
            return -1L;
        }
        if (const auto validated = Core::Validate(*config); !validated.has_value()) {
            set_error(error, error_capacity, validated.error());
            return -1L;
        }
        Core::BerSweepSettings settings;
        settings.EbN0Db.assign(eb_n0_db, eb_n0_db + count);
        settings.MinErrors = min_errors;
        settings.MaxBits = max_bits;
        settings.BlockSymbols = block_symbols;
        const auto points = Core::BerSweep(*config, settings);
        if (!points.has_value()) {
            set_error(error, error_capacity, points.error());
            return -1L;
        }
        if (out)
            for (std::size_t k = 0; k < points->size(); ++k) {
                const auto& p = (*points)[k];
                out[5 * k] = p.EbN0Db; out[5 * k + 1] = p.SnrDb;
                out[5 * k + 2] = static_cast<double>(p.Errors.BitCount); out[5 * k + 3] = static_cast<double>(p.Errors.BitErrorCount);
                out[5 * k + 4] = p.Theory ? *p.Theory : std::numeric_limits<double>::quiet_NaN();
            }
        return static_cast<long>(points->size());
    });
}

long siggen_eye(const siggen_signal* s, std::size_t max_traces, double* time, std::size_t time_capacity, double* i, double* q,
                std::size_t trace_capacity, std::size_t* points) {
    return guarded(nullptr, 0, -1L, [&] {
        if (!s->has_config || s->signal.Samples.size() != s->expected_samples) return -1L;
        const auto eye = Core::BuildEyeDiagram(s->signal, max_traces);
        if (!eye.has_value()) return -1L;
        const std::size_t count = eye->InPhase.size();
        const std::size_t width = eye->TimeSymbols.size();
        if (points) *points = width;
        if (time && time_capacity >= width) std::copy(eye->TimeSymbols.begin(), eye->TimeSymbols.end(), time);
        if (i && q && trace_capacity >= count * width)
            for (std::size_t k = 0; k < count; ++k) {
                std::copy(eye->InPhase[k].begin(), eye->InPhase[k].end(), i + k * width);
                std::copy(eye->Quadrature[k].begin(), eye->Quadrature[k].end(), q + k * width);
            }
        return static_cast<long>(count);
    });
}

long siggen_matched_symbols(const siggen_signal* s, float* out, std::size_t* symbol_indices, std::size_t capacity) {
    return guarded(nullptr, 0, -1L, [&] {
        if (!s->has_config || s->signal.Samples.size() != s->expected_samples || s->signal.Symbols.empty()) return -1L;
        const auto observations = Core::MatchedSymbols(s->signal);
        if (!observations.has_value()) return -1L;
        const auto count = observations->Values.size();
        if (out && symbol_indices && capacity >= count)
            for (std::size_t k = 0; k < count; ++k) {
                out[2 * k] = observations->Values[k].real();
                out[2 * k + 1] = observations->Values[k].imag();
                symbol_indices[k] = observations->SymbolIndices[k];
            }
        return static_cast<long>(count);
    });
}

long siggen_psd(const float* interleaved, std::size_t n, double sample_rate_hz, std::size_t segment_length, int window,
                double* frequency_hz, double* density, std::size_t capacity, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, -1L, [&] {
        if (window < 0 || window > 3) {
            set_error(error, error_capacity, "window must be 0..3");
            return -1L;
        }
        std::vector<std::complex<float>> samples(n);
        for (std::size_t k = 0; k < n; ++k) samples[k] = {interleaved[2 * k], interleaved[2 * k + 1]};
        const auto psd = Core::WelchPsd(samples, sample_rate_hz, segment_length, static_cast<Core::Window>(window));
        if (!psd.has_value()) {
            set_error(error, error_capacity, psd.error());
            return -1L;
        }
        const auto bins = psd->FrequencyHz.size();
        if (frequency_hz && density && capacity >= bins) {
            std::copy(psd->FrequencyHz.begin(), psd->FrequencyHz.end(), frequency_hz);
            std::copy(psd->PowerDensity.begin(), psd->PowerDensity.end(), density);
        }
        return static_cast<long>(bins);
    });
}

int siggen_export(const siggen_signal* s, const char* path, int format, int overwrite, char* error, std::size_t error_capacity) {
    return guarded(error, error_capacity, -1, [&] {
        if (!s->has_config) {
            set_error(error, error_capacity, "Only signals generated by siggen can be exported");
            return -1;
        }
        if (format < 0 || format > 1) {
            set_error(error, error_capacity, "format must be 0 (csv) or 1 (sigmf)");
            return -1;
        }
        const auto kind = format == 0 ? Core::ExportFormat::CSV : Core::ExportFormat::SigMF;
        const auto exported = Core::ExportSignal(path ? path : "", s->signal, kind, overwrite != 0);
        if (!exported.has_value()) {
            set_error(error, error_capacity, exported.error());
            return -1;
        }
        return 0;
    });
}
}
