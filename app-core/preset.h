/**
 * @file    preset.h
 * @brief   Reads and writes generation settings as INI-like preset files.
 */

#ifndef SIGGEN_PRESET_H
#define SIGGEN_PRESET_H

#include "generator.h"
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace Core {

std::expected<std::string, std::string> SerializePreset(const GenerationConfig &config);
std::expected<GenerationConfig, std::string> ParsePreset(std::string_view text);
std::string PresetNotes(std::string_view text);

std::expected<void, std::string> SavePreset(const std::filesystem::path &path, const GenerationConfig &config);
std::expected<GenerationConfig, std::string> LoadPreset(const std::filesystem::path &path);
std::expected<std::string, std::string> LoadPresetNotes(const std::filesystem::path &path);

} // namespace Core

#endif // SIGGEN_PRESET_H
