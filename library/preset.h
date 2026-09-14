#pragma once
#include "generator.h"
#include <filesystem>
#include <string_view>
namespace iq {
std::string serialize_preset(const GenerationConfig& config);
GenerationConfig parse_preset(std::string_view text);
void save_preset(const std::filesystem::path& path, const GenerationConfig& config);
GenerationConfig load_preset(const std::filesystem::path& path);
}
