/*
 * MariusFX — custom ImGui theme.
 *
 * Single source of truth for the palette / paddings / roundings used
 * across the overlay. Call `apply_theme()` once during runtime init
 * (before any ImGui::Begin) and the rest of the UI uses ImGui's
 * normal API while inheriting the look defined here.
 */

#pragma once

#include <imgui.h>

namespace mariusfx::ui::theme {

// ── Palette ─────────────────────────────────────────────────────────────────
// Colours are written as IM_COL32(r, g, b, a) so they survive any
// gamma/colorspace conversion ImGui applies to the style array.

namespace col {
// Backgrounds — glassmorphism-inspired with subtle gradients
constexpr ImU32 bg_app          = IM_COL32(0x0A, 0x0C, 0x12, 0xFF);  // Deeper, richer black
constexpr ImU32 bg_sidebar      = IM_COL32(0x0D, 0x0F, 0x16, 0xF8);  // Slightly translucent
constexpr ImU32 bg_icon_sidebar = IM_COL32(0x08, 0x0A, 0x0F, 0xFF);  // Darker icon sidebar
constexpr ImU32 bg_titlebar     = IM_COL32(0x0A, 0x0C, 0x12, 0xFA);  // Subtle transparency
constexpr ImU32 bg_bottombar    = IM_COL32(0x0D, 0x0F, 0x16, 0xF8);
constexpr ImU32 bg_card         = IM_COL32(0xFF, 0xFF, 0xFF, 0x0A);  // More visible glass effect
constexpr ImU32 bg_card_hover   = IM_COL32(0xFF, 0xFF, 0xFF, 0x18);  // Stronger hover
constexpr ImU32 bg_input        = IM_COL32(0xFF, 0xFF, 0xFF, 0x0C);  // Slightly more visible
constexpr ImU32 bg_input_focus  = IM_COL32(0x4F, 0x7E, 0xF8, 0x18);  // Stronger focus glow

// Borders — softer, more refined
constexpr ImU32 border_default  = IM_COL32(0xFF, 0xFF, 0xFF, 0x12);  // More visible
constexpr ImU32 border_subtle   = IM_COL32(0xFF, 0xFF, 0xFF, 0x08);  // Subtle inner borders
constexpr ImU32 border_accent   = IM_COL32(0x4F, 0x7E, 0xF8, 0x60);  // Stronger accent

// Shadows — for depth and elevation
constexpr ImU32 shadow_soft     = IM_COL32(0x00, 0x00, 0x00, 0x30);  // Card shadows
constexpr ImU32 shadow_strong   = IM_COL32(0x00, 0x00, 0x00, 0x50);  // Modal shadows

// Text — five tiers, from primary down to "almost invisible label"
constexpr ImU32 text_primary    = IM_COL32(0xE2, 0xEA, 0xF4, 0xFF);
constexpr ImU32 text_secondary  = IM_COL32(0xC8, 0xD6, 0xE8, 0xFF);
constexpr ImU32 text_muted      = IM_COL32(0x8A, 0x9B, 0xB0, 0xFF);
constexpr ImU32 text_dim        = IM_COL32(0x5E, 0x6E, 0x84, 0xFF);
constexpr ImU32 text_dimmer     = IM_COL32(0x3D, 0x4A, 0x5C, 0xFF);
constexpr ImU32 text_dimmest    = IM_COL32(0x2E, 0x3A, 0x48, 0xFF);
constexpr ImU32 text_disabled   = IM_COL32(0x1E, 0x2A, 0x38, 0xFF);

// Accents — vibrant gradient-ready colors
constexpr ImU32 accent          = IM_COL32(0x5B, 0x8D, 0xFF, 0xFF);  // Brighter, more vibrant blue
constexpr ImU32 accent_hover    = IM_COL32(0x7A, 0xA8, 0xFF, 0xFF);  // Lighter on hover
constexpr ImU32 accent_subtle   = IM_COL32(0x5B, 0x8D, 0xFF, 0x20);  // Subtle glow
constexpr ImU32 accent_strong   = IM_COL32(0xA8, 0xC5, 0xFF, 0xFF);  // Bright highlight
constexpr ImU32 accent_gradient = IM_COL32(0x3D, 0x6F, 0xE8, 0xFF);  // Gradient end

// Stats ribbon
constexpr ImU32 stat_fps        = IM_COL32(0x3C, 0xD9, 0x90, 0xFF);
constexpr ImU32 stat_gpu        = IM_COL32(0xF5, 0xA6, 0x23, 0xFF);
constexpr ImU32 stat_cpu        = IM_COL32(0xF5, 0x53, 0x53, 0xFF);
constexpr ImU32 stat_lat        = IM_COL32(0x5E, 0x6E, 0x84, 0xFF);

// Window-control dots (purely cosmetic mac-style traffic lights)
constexpr ImU32 dot_red         = IM_COL32(0xFF, 0x5F, 0x57, 0xFF);
constexpr ImU32 dot_yellow      = IM_COL32(0xFF, 0xBD, 0x2E, 0xFF);
constexpr ImU32 dot_green       = IM_COL32(0x27, 0xC9, 0x3F, 0xFF);

// Warning ribbon (used by 'Performance mode' button)
constexpr ImU32 warn_bg         = IM_COL32(0xF5, 0xA6, 0x23, 0x12);
constexpr ImU32 warn_border     = IM_COL32(0xF5, 0xA6, 0x23, 0x2E);
constexpr ImU32 warn_text       = IM_COL32(0xC8, 0x86, 0x1F, 0xFF);

// Save-button green
constexpr ImU32 save_bg         = IM_COL32(0x34, 0xD9, 0x90, 0x14);
constexpr ImU32 save_border     = IM_COL32(0x34, 0xD9, 0x90, 0x2E);
constexpr ImU32 save_text       = IM_COL32(0x3C, 0xD9, 0x90, 0xFF);
} // namespace col

// ── Layout constants ────────────────────────────────────────────────────────
// Read by both theme.cpp (when configuring ImGuiStyle) and ui.cpp (for
// custom-drawn pieces like the titlebar dots).

namespace size {
constexpr float titlebar_height   = 48.0f;   // Taller, more spacious
constexpr float bottombar_height  = 52.0f;   // More breathing room
constexpr float sidebar_width     = 300.0f;  // Wider for better readability
constexpr float icon_sidebar_w    = 64.0f;   // Vertical icon nav bar (Map Studio style)
constexpr float header_h          = 72.0f;   // Header bar height (title + subtitle)
constexpr float preview_width     = 220.0f;  // Larger previews

constexpr float radius_card       = 12.0f;   // Smoother curves
constexpr float radius_button     = 8.0f;    // More rounded
constexpr float radius_input      = 10.0f;   // Softer inputs
constexpr float radius_pill       = 24.0f;   // Fuller pills
constexpr float radius_shader_row = 10.0f;   // Smoother rows

// Spacing — more generous, modern
constexpr float spacing_xs        = 4.0f;
constexpr float spacing_sm        = 8.0f;
constexpr float spacing_md        = 12.0f;
constexpr float spacing_lg        = 16.0f;
constexpr float spacing_xl        = 24.0f;
} // namespace size

// Apply our palette + paddings to the global ImGuiStyle. Idempotent —
// safe to call multiple times across reloads.
void apply_theme();

// Convert IM_COL32 to ImVec4 (some ImGui APIs only take ImVec4).
inline ImVec4 to_vec4(ImU32 c) {
    return ImVec4(
        ((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
        ((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
        ((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
        ((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
}

} // namespace mariusfx::ui::theme
