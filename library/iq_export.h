#pragma once
#include "generator.h"
#include <filesystem>
#include <ostream>
#include <span>
#include <string_view>
namespace iq {
enum class ExportFormat { CSV, BinaryFloat32 };
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
