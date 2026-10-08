/**
 * @file    iq_export.h
 * @brief   Writes generated I/Q samples as CSV with a JSON sidecar, or as a SigMF recording.
 */

#ifndef SIGGEN_IQ_EXPORT_H
#define SIGGEN_IQ_EXPORT_H

#include "generator.h"
#include <complex>
#include <filesystem>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

namespace Core {

/**
 * @enum    ExportFormat
 * @brief   Sample file formats: CSV text (time_s,i,q), or SigMF: interleaved I/Q float32 little-endian
 *          (`cf32_le`) in a `.sigmf-data` file with a `.sigmf-meta` JSON beside it.
 */
enum class ExportFormat {
    CSV,
    SigMF
};

std::filesystem::path MetadataPath(const std::filesystem::path &destination);
std::string JsonQuote(std::string_view text);
std::string ExportMetadata(const GeneratedSignal &signal, ExportFormat format);

void WriteSamples(std::ostream &stream, std::span<const std::complex<float>> samples, double sample_rate_hz,
                  ExportFormat format);
void WriteSamples(std::ostream &stream, const GeneratedSignal &signal, ExportFormat format);
void ExportSignal(const std::filesystem::path &destination, const GeneratedSignal &signal, ExportFormat format,
                  bool overwrite = false);

} // namespace Core

#endif // SIGGEN_IQ_EXPORT_H
