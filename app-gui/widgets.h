/**
 * @file    widgets.h
 * @brief   Small ImGui helpers shared by the generator windows: hints, help buttons, metrics and colours.
 */

#ifndef SIGGEN_WIDGETS_H
#define SIGGEN_WIDGETS_H

#include "help_topics.h"
#include "imgui.h"
#include <string>

namespace GUI {

constexpr ImVec4 ErrorColor(.96f, .45f, .40f, 1.f);
constexpr ImVec4 WarningColor(.95f, .75f, .30f, 1.f);
// Plot colours of the I and Q components; Core::InPhaseColor and Core::QuadratureColor match them in exported images
constexpr ImVec4 InPhaseColor(.2f, .6f, 1.f, 1.f);
constexpr ImVec4 QuadratureColor(1.f, .55f, .15f, 1.f);

void Hint(const char *text);
void HelpButton(const HelpTopic &topic, const std::string &live = {}, bool inside_disabled = false);
void Metric(const char *label, const std::string &value, bool first = false);
std::string FormatNumber(const char *format, double value);

} // namespace GUI

#endif // SIGGEN_WIDGETS_H
