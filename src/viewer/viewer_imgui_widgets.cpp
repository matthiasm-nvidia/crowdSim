#include "viewer_imgui_widgets.h"
#include "imgui.h"
#include "viewer_imgui_theme.h"
namespace viewer::imgui_widgets
{
namespace
{
constexpr ImU32 k_border = IM_COL32(120, 120, 120, 255);
constexpr ImU32 k_text = IM_COL32(224, 224, 224, 255);
constexpr float k_corner_radius = 3.0f;
float scaled(float value)
{
    return imgui_theme::ui(value);
}
} // namespace
void section_heading(const char *text)
{
    ImFont *bold_font = viewer::imgui_theme::ui_bold_font();
    if (bold_font != nullptr)
    {
        ImGui::PushFont(bold_font);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, k_text);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();

    if (bold_font != nullptr)
    {
        ImGui::PopFont();
    }
}

bool button(const char *label, float width)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, scaled(k_corner_radius));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(scaled(10.0f), scaled(7.0f)));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(30, 100, 156, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(37, 121, 187, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(23, 78, 122, 255));

    ImFont *bold_font = viewer::imgui_theme::ui_bold_font();
    if (bold_font != nullptr)
    {
        ImGui::PushFont(bold_font);
    }

    const bool pressed = ImGui::Button(label, ImVec2(width, 0.0f));

    if (bold_font != nullptr)
    {
        ImGui::PopFont();
    }

    if (ImGui::IsItemVisible())
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRect(min, max, k_border, scaled(k_corner_radius));
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    return pressed;
}

bool button_full_width(const char *label)
{
    return button(label, ImGui::GetContentRegionAvail().x);
}

} // namespace viewer::imgui_widgets
