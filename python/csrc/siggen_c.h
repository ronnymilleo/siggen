// Plain C interface to the siggen library, loaded from Python with ctypes.
// Every function catches C++ exceptions: failures return NULL or a negative
// value, and the message is copied into the caller-supplied `error` buffer.
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SIGGEN_API __attribute__((visibility("default")))
typedef struct siggen_signal siggen_signal;

SIGGEN_API const char* siggen_version(void);
// Configuration text of a default signal (the preset format), the base that Python edits.
SIGGEN_API int siggen_default_preset(char* out, size_t capacity);

// Generate from preset text. Returns NULL on failure.
SIGGEN_API siggen_signal* siggen_generate(const char* preset_text, char* error, size_t error_capacity);
// Read a SigMF recording (.sigmf-meta/.sigmf-data) or a batch cf32 frame. Returns NULL on failure.
SIGGEN_API siggen_signal* siggen_load(const char* path, char* error, size_t error_capacity);
SIGGEN_API void siggen_free(siggen_signal* signal);

SIGGEN_API size_t siggen_sample_count(const siggen_signal* signal);
SIGGEN_API double siggen_sample_rate(const siggen_signal* signal);
SIGGEN_API size_t siggen_filter_delay(const siggen_signal* signal);
// Interleaved float32 I/Q; `out` holds 2 * sample_count floats.
SIGGEN_API void siggen_copy_samples(const siggen_signal* signal, float* out);
// Replace the samples (for example after adding noise in NumPy). `n` may differ
// from the generated length; measurements that need the original length then fail.
SIGGEN_API int siggen_set_samples(siggen_signal* signal, const float* interleaved, size_t n, char* error, size_t error_capacity);
SIGGEN_API size_t siggen_symbol_count(const siggen_signal* signal);
SIGGEN_API void siggen_copy_symbols(const siggen_signal* signal, float* out);
// Preset text describing the configuration; returns the length required (excluding NUL), or -1 when unavailable.
SIGGEN_API long siggen_preset_text(const siggen_signal* signal, char* out, size_t capacity);
SIGGEN_API long siggen_waveform_name(const siggen_signal* signal, char* out, size_t capacity);
SIGGEN_API long siggen_bits(const siggen_signal* signal, char* out, size_t capacity);

// out[3] = mean power, peak power, PAPR (dB).
SIGGEN_API int siggen_power_statistics(const siggen_signal* signal, double* out);
// out[5] = symbol count, EVM (fraction), EVM (dB), SNR after matched filter (dB), 10 log10(SPS).
// Returns 1 when available, 0 when the signal has no constellation or no configuration, -1 on failure.
SIGGEN_API int siggen_symbol_accuracy(const siggen_signal* signal, double* out, char* error, size_t error_capacity);
// Eye diagram traces: fills `time` (points) and `i`/`q` (traces * points); returns trace count, or -1.
SIGGEN_API long siggen_eye(const siggen_signal* signal, size_t max_traces, double* time, size_t time_capacity,
                           double* i, double* q, size_t trace_capacity, size_t* points);

// Matched-filter decision observations (interleaved I/Q) and the index of the symbol each one belongs to.
// Returns the count (or -1); the arrays are filled when non-NULL and `capacity` observations fit.
SIGGEN_API long siggen_matched_symbols(const siggen_signal* signal, float* out, size_t* symbol_indices, size_t capacity);

// Two-sided Welch PSD of interleaved I/Q. window: 0 Hann, 1 Hamming, 2 Blackman, 3 Rectangular.
// Returns the bin count; frequency and density are filled when non-NULL and `capacity` is large enough.
SIGGEN_API long siggen_psd(const float* interleaved, size_t n, double sample_rate_hz, size_t segment_length, int window,
                           double* frequency_hz, double* density, size_t capacity, char* error, size_t error_capacity);

// format: 0 CSV, 1 SigMF. Requires a signal that carries its configuration.
SIGGEN_API int siggen_export(const siggen_signal* signal, const char* path, int format, int overwrite, char* error, size_t error_capacity);
#ifdef __cplusplus
}
#endif
