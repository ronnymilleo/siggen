/***********************************************************************************************************************
 *
 * @file ImGuiWindow.h
 * @brief ImGuiWindow class prototype.
 *
 **********************************************************************************************************************/

#pragma once

/***********************************************************************************************************************
 * DEPENDENCIES
 **********************************************************************************************************************/

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include <string>

/***********************************************************************************************************************
 * MACROS AND TYPEDEFS
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * CLASS
 **********************************************************************************************************************/

class ImGuiWindowLayer
{
public:
    explicit ImGuiWindowLayer(const std::string& title) : m_WindowTitle{title}
    {
    }
    virtual ~ImGuiWindowLayer() = default;
    virtual void Render()
    {
        const auto* viewport = ImGui::GetMainViewport();
        // Always follow the native window, including after resize or loading an
        // old imgui.ini that placed the generator in a detached viewport.
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                               ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin(m_WindowTitle.c_str(), nullptr, flags))
            DrawContents();
        ImGui::End();
    }

protected:
    virtual void DrawContents() = 0;

private:
    std::string m_WindowTitle;
};

/***********************************************************************************************************************
 * END OF FILE
 **********************************************************************************************************************/
