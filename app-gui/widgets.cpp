/**
 * @file    widgets.cpp
 * @brief   Small ImGui helpers shared by the generator windows: hints, help buttons, metrics and colours.
 */

#include "widgets.h"

#include <cstdio>

namespace GUI {

/**
 * @brief   Shows plain-language help while the preceding control is hovered.
 * @param[in] text  Tooltip text, wrapped at 24 font sizes.
 */
void Hint(const char *text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

/**
 * @brief   Draws a "?" button beside the preceding control that opens a panel explaining the setting.
 * @param[in] topic            The explanation to show.
 * @param[in] live             Optional extra line computed from the current settings.
 * @param[in] inside_disabled  True when called between BeginDisabled() and EndDisabled(); the button then stays
 *                             enabled, so the explanation is readable while its control is greyed out.
 */
void HelpButton(const HelpTopic &topic, const std::string &live, bool inside_disabled) {
    if (inside_disabled) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::PushID(topic.Id.data(), topic.Id.data() + topic.Id.size());
    if (ImGui::SmallButton("?")) {
        ImGui::OpenPopup("help");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip("What is this? Click for an explanation.");
    }
    if (ImGui::BeginPopup("help")) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.f);
        ImGui::TextColored(ImVec4(.55f, .75f, 1.f, 1.f), "%.*s", static_cast<int>(topic.Title.size()),
                           topic.Title.data());
        ImGui::Separator();
        ImGui::TextUnformatted(topic.Body.data(), topic.Body.data() + topic.Body.size());
        if (!live.empty()) {
            ImGui::Separator();
            ImGui::TextUnformatted(live.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    if (inside_disabled) {
        ImGui::BeginDisabled();
    }
}

/**
 * @brief   Shows a caption over a value, as one item of a row of metrics.
 * @param[in] label  Caption, drawn dimmed.
 * @param[in] value  Value text.
 * @param[in] first  True to start a new row instead of continuing the current one.
 */
void Metric(const char *label, const std::string &value, bool first) {
    if (!first) {
        ImGui::SameLine(0, 28);
    }
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", label);
    ImGui::TextUnformatted(value.c_str());
    ImGui::EndGroup();
}

/**
 * @brief   Formats one number with a printf format.
 * @param[in] format  printf format with a single floating-point conversion.
 * @param[in] value   The number.
 * @return  The formatted text, truncated to 47 characters.
 */
std::string FormatNumber(const char *format, double value) {
    char text[48];
    std::snprintf(text, sizeof text, format, value);
    return text;
}

} // namespace GUI
