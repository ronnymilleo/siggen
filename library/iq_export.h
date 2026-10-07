#pragma once
#include "generator.h"
#include <complex>
#include <filesystem>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
namespace iq {
// SigMF writes little-endian float32 I/Q (`cf32_le`) to a `.sigmf-data` file,
// with a `.sigmf-meta` JSON beside it.
enum class ExportFormat { CSV, SigMF };
// `<destination>.json`, or the `.sigmf-meta` sibling of a `.sigmf-data` destination.
std::filesystem::path metadata_path(const std::filesystem::path& destination);
// Minimal JSON string escaping shared by export and batch sidecars/manifests.
std::string json_quote(std::string_view text);
std::string export_metadata(const GeneratedSignal& signal, ExportFormat format);
void write_samples(std::ostream& stream, std::span<const std::complex<float>> samples,
                   double sample_rate_hz, ExportFormat format);
void write_samples(std::ostream& stream, const GeneratedSignal& signal, ExportFormat format);
void export_signal(const std::filesystem::path& destination, const GeneratedSignal& signal,
                   ExportFormat format, bool overwrite = false);
}
