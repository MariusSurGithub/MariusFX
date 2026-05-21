/*
 * MariusFX overlay UI  top-level entry point.
 *
 * The whole UI is a self-contained pipeline editor:
 *    Toolbar (52 px)         search, filter chips, panel-mode icons.
 *    Pipeline column         unified list of effects in pipeline order,
 *                              drag-and-drop reorder, in-place enable/
 *                              disable for trivial before/after testing.
 *    Right panel (flex)      params editor, performance stats, or
 *                              workspace settings depending on mode.
 *    Statusbar (28 px)       fps  ms  active count  saving badge 
 *                              preset name  build version.
 *
 * It speaks to the host through the public effect_runtime API
 * (enumerate_techniques / get/set_technique_state /
 * enumerate_uniform_variables / get/set_uniform_value_*) plus four
 * non-virtual extensions surfaced via the MfxuiHostAPI bridge in
 * exports.hpp (per-tech timings + performance mode + reload).
 */

#include "ui.hpp"
#include "theme.hpp"
#include "exports.hpp" // MfxuiHostAPI struct used by the rt_*/host_* wrappers

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>  // ShellExecuteA  used by the row context menu's "Open .fx"

#include <imgui.h>
#include <imgui_internal.h>

// ── Build-flavour selector ─────────────────────────────────────────────────
//
// MARIUSFX_PREVIEW : standalone preview .exe — links against a self-
//                    contained mock runtime in preview/preview_runtime.cpp.
// MARIUSFX_HOT_DLL : hot-reloadable DLL build. ui.cpp lives in
//                    MariusFXUI.dll and is loaded by the host at runtime.
//                    We must NOT static-link against the host's runtime
//                    implementation, so we alias `mfx::runtime` to its
//                    public base and route the four non-virtual host
//                    extensions through the MfxuiHostAPI bridge installed
//                    at init time.
// (default)        : in-process build that links against the host's
//                    runtime implementation directly.
#ifdef MARIUSFX_PREVIEW
  #include "preview/preview_runtime.hpp"
#elif defined(MARIUSFX_HOT_DLL)
  #include "../../include/reshade_api.hpp"
  #include "../../include/reshade_api_format.hpp"
  namespace reshade { using runtime = api::effect_runtime; }
#else
  #include "../../source/runtime.hpp"
  #include "../../include/reshade_api.hpp"
  #include "../../include/reshade_api_format.hpp"
#endif

// ── Internal aliases ───────────────────────────────────────────────────────
//
// All MariusFX code below talks about `mfx::runtime` / `mfx::api` rather
// than the underlying `reshade::*` names. The two are the same types — we
// just centralise the binding here so the rest of the file reads as a
// self-contained MariusFX module. The `reshade::` namespace is technically
// reachable via the api header above, but no helper or panel function is
// allowed to spell it: they must use `mfx::*`.
namespace mfx {
    namespace api  = ::reshade::api;
    using runtime  = ::reshade::runtime;
}

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace mariusfx::ui {

// Host API bridge pointer. Defined in exports.cpp; declared in the named
// namespace (NOT in the anonymous namespace below) so its external linkage
// matches the definition.
#ifdef MARIUSFX_HOT_DLL
extern const MfxuiHostAPI *g_host_api;
#endif

namespace {

// Logical modules. See ui_<name>.inl for the actual code. Each file is
// pulled into the anonymous namespace above so internal helpers keep
// file-scope linkage without polluting the public API surface.
#include "ui_state.inl"
#include "ui_chrome.inl"
#include "ui_pipeline.inl"
#include "ui_params.inl"
#include "ui_panels.inl"

} // namespace

// ── Public API ─────────────────────────────────────────────────────────────
void init()
{
    theme::apply_theme();
    ++g_load_count;
    // Use ImGui's frame time as a wall clock  it's available no matter
    // what host is calling us, and we only need a relative number.
    g_load_time_sec = ImGui::GetTime();
}

// ── Keyboard shortcuts ──────────────────────────────────────────────────────
// Centralised so every accelerator behaviour lives in one place. Only fires
// when the user isn't currently typing in a text input (ImGui sets the
// WantTextInput flag for that case).
void handle_keyboard(mfx::runtime *rt)
{
    const ImGuiIO &io = ImGui::GetIO();
    if (io.WantTextInput) {
        // Even inside a text input, Esc should still let the user back out.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && g_toolbar_search[0]) {
            g_toolbar_search[0] = '\0';
        }
        return;
    }
    const bool ctrl  = io.KeyCtrl;

    // Ctrl+F  focus the shader search box.
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
        g_focus_search_next = true;
    }
    // Ctrl+R  full effect reload.
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_R)) {
        rt_reload_all(rt);
    }
    // Tab  cycle right-panel mode (Params → Stats → Settings → Params).
    if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !io.KeyShift && !ctrl) {
        if      (g_active_sheet == SHEET_NONE)       g_active_sheet = SHEET_STATISTICS;
        else if (g_active_sheet == SHEET_STATISTICS) g_active_sheet = SHEET_SETTINGS;
        else                                         g_active_sheet = SHEET_NONE;
    }
    // Esc  cascade: clear search → close sheet → clear selection.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        if      (g_toolbar_search[0])         g_toolbar_search[0] = '\0';
        else if (g_active_sheet != SHEET_NONE) g_active_sheet = SHEET_NONE;
        else                                   g_selected_key[0] = '\0';
    }
    // ↑/↓  navigate the (visible/filtered) shader list, wrapping around.
    auto match_visible = [&](const TechRow &t) {
        if (t.hidden) return false;
        if (!g_toolbar_search[0]) return true;
        return icase_contains(t.name, g_toolbar_search) ||
               icase_contains(t.effect_short, g_toolbar_search);
    };
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
        ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    {
        const int dir = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? +1 : -1;
        const int n   = (int)g_techs.size();
        if (n > 0) {
            int cur = find_selected_index();
            if (cur < 0) cur = (dir > 0 ? -1 : 0);
            // Step until we land on a visible row, or give up after n steps.
            for (int step = 0; step < n; ++step) {
                cur = (cur + dir + n) % n;
                if (match_visible(g_techs[cur])) {
                    const TechRow &t = g_techs[cur];
                    make_key(g_selected_key, sizeof(g_selected_key),
                             t.effect_full, t.name);
                    break;
                }
            }
        }
    }
    // Space  toggle the currently-selected technique.
    if (ImGui::IsKeyPressed(ImGuiKey_Space)) {
        const int i = find_selected_index();
        if (i >= 0) {
            TechRow &t = g_techs[i];
            rt->set_technique_state(t.handle, !t.enabled);
        }
    }
}

void render(mfx::runtime *rt)
{
    if (rt == nullptr) return;

    push_frametime_sample();
    refresh_tech_list(rt);
    handle_keyboard(rt);

    // Hand cursor stays asserted as long as a tech-row drag is in flight,
    // even if the cursor leaves the pipeline column (e.g. over the params
    // panel or the resize rail). ImGui resets the cursor every frame so
    // we have to set it every frame too.
    if (g_drag_src >= 0)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const ImVec2 win_pos  = ImGui::GetWindowPos();
    const ImVec2 win_size = ImGui::GetWindowSize();

    // Remember the user-resized width so configure_next_window snaps to
    // the dock edge with the right offset on the next frame.
    g_window_width = win_size.x;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0, 0));

    // ── Map Studio layout ──────────────────────────────────────────────
    //
    //  ┌──────┬──────────────────────────────────────┐
    //  │ ICON │ HEADER (title + preset + actions)     │
    //  │ BAR  ├──────────────────┬───────────────────┤
    //  │      │ Pipeline list    │ Params / Stats /  │
    //  │      │ (shader list)    │ Settings panel    │
    //  │      │                  │                   │
    //  └──────┴──────────────────┴───────────────────┘
    //
    constexpr float sidebar_w = 64.0f;
    constexpr float header_h  = 72.0f;

    // App background.
    ImGui::GetWindowDrawList()->AddRectFilled(
        win_pos, ImVec2(win_pos.x + win_size.x, win_pos.y + win_size.y),
        theme::col::bg_app);

    // Icon sidebar (full height, left edge).
    pl_draw_icon_sidebar(win_pos, sidebar_w, win_size.y);

    // Header bar (next to sidebar, top).
    const ImVec2 header_origin(win_pos.x + sidebar_w, win_pos.y);
    const float  header_w = win_size.x - sidebar_w;
    pl_draw_header(rt, header_origin, header_w, header_h);

    // Content area (below header, right of sidebar).
    const float content_x = win_pos.x + sidebar_w;
    const float content_y = win_pos.y + header_h;
    const float content_w = win_size.x - sidebar_w;
    const float content_h = win_size.y - header_h;

    // Two columns: pipeline (left ~42% of content) and right panel.
    float pipe_w = content_w * 0.42f;
    if (pipe_w < 220.0f) pipe_w = 220.0f;
    if (pipe_w > 400.0f) pipe_w = 400.0f;
    const float params_w = content_w - pipe_w;

    pl_draw_pipeline(rt, ImVec2(content_x, content_y), pipe_w, content_h);

    // Right column dispatches by active mode  tab-style swap, no overlay.
    const ImVec2 right_origin(content_x + pipe_w, content_y);
    switch (g_active_sheet) {
    case SHEET_STATISTICS:
        pl_draw_stats_panel   (rt, right_origin, params_w, content_h);
        break;
    case SHEET_SETTINGS:
        pl_draw_settings_panel(rt, right_origin, params_w, content_h);
        break;
    default:
        pl_draw_param_editor  (rt, right_origin, params_w, content_h);
        break;
    }

    // Preset picker (existing, unchanged).
    draw_preset_popup(rt, win_pos, win_size);

    // (Esc / Tab / ↑↓ / Space / Ctrl+F / Ctrl+R are handled centrally in
    // handle_keyboard() above  kept out of the per-panel rendering code.)

    // ── Resize handle on the inner edge ───────────────────────────────
    // Always-visible 10px rail with grip dots in the middle, so the
    // affordance is obvious without hovering. Drawn last so its hit-test
    // wins over any underlying widget that overlaps the strip.
    {
        using namespace theme;
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const float hw = 10.0f;
        ImVec2 ha, hb;
        if (g_dock_side == DOCK_LEFT) {
            ha = ImVec2(win_pos.x + win_size.x - hw, win_pos.y);
            hb = ImVec2(win_pos.x + win_size.x,      win_pos.y + win_size.y);
        } else {
            ha = ImVec2(win_pos.x,           win_pos.y);
            hb = ImVec2(win_pos.x + hw,      win_pos.y + win_size.y);
        }
        ImGui::SetCursorScreenPos(ha);
        ImGui::InvisibleButton("##mfx_resize", ImVec2(hb.x - ha.x, hb.y - ha.y));
        const bool hov = ImGui::IsItemHovered();
        const bool act = ImGui::IsItemActive();
        if (hov || act) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        if (act && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        {
            const ImVec2 mp = ImGui::GetIO().MousePos;
            float new_w = (g_dock_side == DOCK_LEFT)
                            ? (mp.x - win_pos.x)
                            : (win_pos.x + win_size.x - mp.x);
            if (new_w < kMinWindowWidth) new_w = kMinWindowWidth;
            g_window_width = new_w;
            g_force_size = true;
        }

        // Always-visible rail. Subtle by default, accent on hover/active.
        const ImU32 rail_col = act ? col::accent_subtle
                              : hov ? col::bg_card_hover
                                    : col::bg_titlebar;
        dl->AddRectFilled(ha, hb, rail_col);

        // Hairline against the inner content for visual separation.
        const float edge_x = (g_dock_side == DOCK_LEFT) ? ha.x : hb.x;
        dl->AddLine(ImVec2(edge_x, ha.y), ImVec2(edge_x, hb.y),
                    col::border_default, 1.0f);

        // Grip dots in the middle  5 small dots vertically.
        const ImU32 dot_col = act ? col::accent
                            : hov ? col::text_primary
                                  : col::text_dim;
        const float cx = (ha.x + hb.x) * 0.5f;
        const float cy = (win_pos.y + win_pos.y + win_size.y) * 0.5f;
        for (int i = -2; i <= 2; ++i)
            dl->AddCircleFilled(ImVec2(cx, cy + i * 6.0f), 1.7f, dot_col, 8);
    }

    ImGui::PopStyleVar(2);
}

// ── Public layout setup ─────────────────────────────────────────────────────
ImGuiWindowFlags configure_next_window(ImVec2 viewport_pos, ImVec2 viewport_size)
{
    const float max_w = viewport_size.x * kMaxWindowFrac;
    if (g_window_width > max_w)         g_window_width = max_w;
    if (g_window_width < kMinWindowWidth) g_window_width = kMinWindowWidth;

    const float x = (g_dock_side == DOCK_LEFT)
                        ? 0.0f
                        : (viewport_size.x - g_window_width);

    // Pos + height: snapped to the viewport edge every frame.
    ImGui::SetNextWindowPos (ImVec2(viewport_pos.x + x, viewport_pos.y), ImGuiCond_Always);
    // Width is forced only right after init / dock-toggle / custom resize;
    // otherwise we let ImGui keep it at its current value to avoid jank.
    if (g_force_size)
    {
        ImGui::SetNextWindowSize(ImVec2(g_window_width, viewport_size.y), ImGuiCond_Always);
        g_force_size = false;
    }
    else
    {
        // Height every frame (snap to viewport height); width-only constraint
        // already applied separately when the user dragged the custom handle.
        ImGui::SetNextWindowSize(ImVec2(g_window_width, viewport_size.y), ImGuiCond_Always);
    }
    // Lock height; width is bounded by [min, viewport*frac].
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(kMinWindowWidth, viewport_size.y),
        ImVec2(max_w,           viewport_size.y));

    return ImGuiWindowFlags_NoDecoration |
           ImGuiWindowFlags_NoNav        |
           ImGuiWindowFlags_NoMove       |
           ImGuiWindowFlags_NoResize     |
           ImGuiWindowFlags_NoDocking    |
           ImGuiWindowFlags_NoSavedSettings |
           ImGuiWindowFlags_NoBringToFrontOnFocus;
}

// ── Persistent state blob (survives DLL hot-reload) ─────────────────────────
//
// We snapshot the user-visible UI settings into a versioned POD struct
// that the host stores between mfxui_shutdown() and the next mfxui_init().
// Anything safe to lose (per-row hover state, scroll position,
// transient buffers) is intentionally excluded.
namespace {
constexpr unsigned int kPersistMagic   = 0x4D465849u; // 'MFXI'
constexpr unsigned int kPersistVersion = 1;
struct PersistBlob {
    unsigned int magic;
    unsigned int version;
    int          dock_side;
    float        window_width;
    int          active_sheet;
    int          show_available;        // 0/1
    char         selected_key[256];
    char         toolbar_search[160];
    char         preset_search[160];
};
} // namespace

size_t persistent_state_size()
{
    return sizeof(PersistBlob);
}

void persistent_state_save(void *buf, size_t buf_size)
{
    if (buf == nullptr || buf_size < sizeof(PersistBlob)) return;
    PersistBlob b{};
    b.magic           = kPersistMagic;
    b.version         = kPersistVersion;
    b.dock_side       = g_dock_side;
    b.window_width    = g_window_width;
    b.active_sheet    = g_active_sheet;
    b.show_available  = g_show_available ? 1 : 0;
    std::memcpy(b.selected_key,    g_selected_key,    sizeof(b.selected_key));
    std::memcpy(b.toolbar_search,  g_toolbar_search,  sizeof(b.toolbar_search));
    std::memcpy(b.preset_search,   g_preset_search,   sizeof(b.preset_search));
    std::memcpy(buf, &b, sizeof(b));
}

void persistent_state_load(const void *buf, size_t size)
{
    if (buf == nullptr || size < sizeof(PersistBlob)) return;
    PersistBlob b;
    std::memcpy(&b, buf, sizeof(b));
    if (b.magic != kPersistMagic)     return;
    if (b.version != kPersistVersion) return; // silently skip incompatible
    g_dock_side       = b.dock_side;
    g_window_width    = b.window_width;
    g_active_sheet    = b.active_sheet;
    g_show_available  = (b.show_available != 0);
    std::memcpy(g_selected_key,   b.selected_key,   sizeof(g_selected_key));
    std::memcpy(g_toolbar_search, b.toolbar_search, sizeof(g_toolbar_search));
    std::memcpy(g_preset_search,  b.preset_search,  sizeof(g_preset_search));
    g_force_size = true; // re-snap to dock edge with restored width
}

} // namespace mariusfx::ui
