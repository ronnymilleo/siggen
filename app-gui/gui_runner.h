/**
 * @file    gui_runner.h
 * @brief   Entry point of the graphical interface.
 */

#ifndef SIGGEN_GUI_RUNNER_H
#define SIGGEN_GUI_RUNNER_H

#include "generator.h"

namespace GUI {

int RunGUI(const Core::GenerationConfig &config);

} // namespace GUI

#endif // SIGGEN_GUI_RUNNER_H
