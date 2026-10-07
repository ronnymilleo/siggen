#pragma once
#include "generator.h"
#include <filesystem>
#include <string>
#include <string_view>
namespace iq {
std::string serialize_preset(const GenerationConfig& config);
GenerationConfig parse_preset(std::string_view text);
// Lines beginning with '#' after the header are notes: parse_preset ignores them
// and preset_notes returns them (without the marker), joined by newlines. Used
// by the guided presets in presets/ to carry a short lesson.
std::string preset_notes(std::string_view text);
std::string load_preset_notes(const std::filesystem::path& path);
void save_preset(const std::filesystem::path& path, const GenerationConfig& config);
GenerationConfig load_preset(const std::filesystem::path& path);
}
