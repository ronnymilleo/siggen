/**
 * @file    preset.h
 * @brief   Reads and writes generation settings as INI-like preset files.
 */

#ifndef SIGGEN_PRESET_H
#define SIGGEN_PRESET_H

#include "generator.h"
#include <filesystem>
#include <string>
#include <string_view>

namespace Core {

std::string SerializePreset(const GenerationConfig &config);
GenerationConfig ParsePreset(std::string_view text);
std::string PresetNotes(std::string_view text);

void SavePreset(const std::filesystem::path &path, const GenerationConfig &config);
GenerationConfig LoadPreset(const std::filesystem::path &path);
std::string LoadPresetNotes(const std::filesystem::path &path);

} // namespace Core

#endif // SIGGEN_PRESET_H
