/**
 * @file    examples.h
 * @brief   Ready generator settings, the guided lessons, that the user can open from the File menu to learn from.
 */

#ifndef SIGGEN_EXAMPLES_H
#define SIGGEN_EXAMPLES_H

#include <span>
#include <string>
#include <string_view>

namespace GUI {

/**
 * @struct  Example
 * @brief   One guided lesson: its name in the menu, what it shows and the preset text it is read from.
 * @details Files live in app-gui/examples and are compiled into the application; the title is the first note line
 *          of the preset and the description the remaining notes.
 */
struct Example {
    std::string Title;
    std::string Description;
    std::string_view FileName;
    std::string_view Text;
};

/**
 * @brief   Lists the examples the File menu offers, in file order.
 * @return  Every example, the first lesson first.
 */
std::span<const Example> GetExamples();

} // namespace GUI

#endif // SIGGEN_EXAMPLES_H
