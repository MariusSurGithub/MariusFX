/*
 * MariusFX — theme implementation. Pushes our palette + paddings into
 * the global ImGuiStyle so every subsequent ImGui::* call inherits the
 * dark-blue dashboard look.
 */

#include "theme.hpp"

namespace mariusfx::ui::theme {

void apply_theme()
{
    ImGuiStyle &s = ImGui::GetStyle();

    // ── Colours ─────────────────────────────────────────────────────────
    auto set = [&](ImGuiCol slot, ImU32 c) { s.Colors[slot] = to_vec4(c); };

    set(ImGuiCol_WindowBg,            col::bg_app);
    set(ImGuiCol_ChildBg,             col::bg_app);
    set(ImGuiCol_PopupBg,             col::bg_sidebar);
    set(ImGuiCol_Border,              col::border_default);
    set(ImGuiCol_BorderShadow,        IM_COL32(0, 0, 0, 0));

    set(ImGuiCol_FrameBg,             col::bg_input);
    set(ImGuiCol_FrameBgHovered,      col::bg_card_hover);
    set(ImGuiCol_FrameBgActive,       col::bg_input_focus);

    set(ImGuiCol_TitleBg,             col::bg_titlebar);
    set(ImGuiCol_TitleBgActive,       col::bg_titlebar);
    set(ImGuiCol_TitleBgCollapsed,    col::bg_titlebar);

    set(ImGuiCol_MenuBarBg,           col::bg_titlebar);

    set(ImGuiCol_ScrollbarBg,         IM_COL32(0, 0, 0, 0));
    set(ImGuiCol_ScrollbarGrab,       col::border_subtle);
    set(ImGuiCol_ScrollbarGrabHovered, col::text_dim);
    set(ImGuiCol_ScrollbarGrabActive, col::accent);

    set(ImGuiCol_CheckMark,           col::accent);

    set(ImGuiCol_SliderGrab,          col::accent);
    set(ImGuiCol_SliderGrabActive,    col::accent_hover);

    set(ImGuiCol_Button,              col::bg_card);
    set(ImGuiCol_ButtonHovered,       col::bg_card_hover);
    set(ImGuiCol_ButtonActive,        col::accent_subtle);

    set(ImGuiCol_Header,              col::accent_subtle);
    set(ImGuiCol_HeaderHovered,       col::bg_card_hover);
    set(ImGuiCol_HeaderActive,        col::accent_subtle);

    set(ImGuiCol_Separator,           col::border_default);
    set(ImGuiCol_SeparatorHovered,    col::border_accent);
    set(ImGuiCol_SeparatorActive,     col::accent);

    set(ImGuiCol_ResizeGrip,          IM_COL32(0, 0, 0, 0));
    set(ImGuiCol_ResizeGripHovered,   col::border_subtle);
    set(ImGuiCol_ResizeGripActive,    col::accent);

    set(ImGuiCol_Tab,                 col::bg_titlebar);
    set(ImGuiCol_TabHovered,          col::bg_card_hover);
    set(ImGuiCol_TabSelected,         col::bg_titlebar);
    set(ImGuiCol_TabDimmed,           col::bg_titlebar);
    set(ImGuiCol_TabDimmedSelected,   col::bg_titlebar);

    set(ImGuiCol_PlotLines,           col::accent);
    set(ImGuiCol_PlotLinesHovered,    col::accent_hover);
    set(ImGuiCol_PlotHistogram,       col::accent);
    set(ImGuiCol_PlotHistogramHovered, col::accent_hover);

    set(ImGuiCol_TableHeaderBg,       col::bg_sidebar);
    set(ImGuiCol_TableBorderStrong,   col::border_default);
    set(ImGuiCol_TableBorderLight,    col::border_subtle);
    set(ImGuiCol_TableRowBg,          IM_COL32(0, 0, 0, 0));
    set(ImGuiCol_TableRowBgAlt,       col::bg_card);

    set(ImGuiCol_TextSelectedBg,      col::accent_subtle);

    set(ImGuiCol_NavCursor,           col::accent);
    set(ImGuiCol_NavWindowingHighlight, col::accent);
    set(ImGuiCol_NavWindowingDimBg,   IM_COL32(0, 0, 0, 0xCC));
    set(ImGuiCol_ModalWindowDimBg,    IM_COL32(0, 0, 0, 0xCC));

    set(ImGuiCol_Text,                col::text_primary);
    set(ImGuiCol_TextDisabled,        col::text_dimmer);

    // ── Geometry ────────────────────────────────────────────────────────
    s.WindowPadding         = ImVec2(0, 0);    // we own the layout, no implicit padding
    s.FramePadding          = ImVec2(8, 5);
    s.ItemSpacing           = ImVec2(8, 6);
    s.ItemInnerSpacing      = ImVec2(6, 4);
    s.TouchExtraPadding     = ImVec2(0, 0);
    s.IndentSpacing         = 14.0f;
    s.ScrollbarSize         = 6.0f;
    s.GrabMinSize           = 9.0f;
    s.WindowBorderSize      = 1.0f;
    s.ChildBorderSize       = 1.0f;
    s.PopupBorderSize       = 1.0f;
    s.FrameBorderSize       = 1.0f;
    s.TabBorderSize         = 0.0f;

    s.WindowRounding        = 14.0f;
    s.ChildRounding         = size::radius_card;
    s.PopupRounding         = size::radius_card;
    s.FrameRounding         = size::radius_input;
    s.GrabRounding          = 4.0f;
    s.TabRounding           = 0.0f;
    s.ScrollbarRounding     = 1.0f;

    s.WindowTitleAlign      = ImVec2(0.0f, 0.5f);
    s.ButtonTextAlign       = ImVec2(0.5f, 0.5f);
    s.SelectableTextAlign   = ImVec2(0.0f, 0.5f);

    s.AntiAliasedLines      = true;
    s.AntiAliasedFill       = true;
}

} // namespace mariusfx::ui::theme
