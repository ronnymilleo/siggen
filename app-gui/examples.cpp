/**
 * @file    examples.cpp
 * @brief   Ready generator settings, the guided lessons, that the user can open from the File menu to learn from.
 */

#include "examples.h"

#include "example_files.h"
#include "preset.h"
#include <string>
#include <vector>

namespace GUI {

namespace {

// The first note line names the lesson and becomes the menu title; the remaining notes explain it in the tooltip
void SplitNotes(const std::string_view text, std::string &title, std::string &description) {
    const std::string notes = Core::PresetNotes(text);
    const auto line_end = notes.find('\n');
    if (line_end == std::string::npos) {
        title = notes;
        return;
    }
    title = notes.substr(0, line_end);
    description = notes.substr(line_end + 1);
}

std::vector<Example> BuildExamples() {
    std::vector<Example> examples;
    examples.reserve(ExampleFiles.size());
    for (const auto &[file_name, text] : ExampleFiles) {
        Example example;
        example.FileName = file_name;
        example.Text = text;
        SplitNotes(text, example.Title, example.Description);
        if (example.Title.empty()) {
            example.Title = std::string(file_name);
        }
        examples.push_back(std::move(example));
    }
    return examples;
}

} // namespace

/**
 * @brief   Lists the examples the File menu offers, in file order.
 * @return  Every example, the first lesson first.
 */
std::span<const Example> GetExamples() {
    static const std::vector<Example> examples = BuildExamples();
    return examples;
}

} // namespace GUI
