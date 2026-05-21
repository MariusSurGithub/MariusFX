// ============================================================================ // ui_state.inl - included by ui.cpp inside namespace mariusfx::ui::{anonymous}. // This is not a stand-alone translation unit. It exists only as a logical // module to keep ui.cpp browsable. Do not compile or include directly. // ============================================================================ 

namespace api = mfx::api;
using api::effect_runtime;
using api::effect_technique;
using api::effect_uniform_variable;

// ── Runtime extension wrappers ─────────────────────────────────────────────
// Four non-virtual host-runtime methods are not reachable through the
// public effect_runtime base — they're called via the MfxuiHostAPI
// bridge in the hot-reload DLL flavour, or directly in the production /
// preview builds. The wrappers below pick the right path at compile time
// and degrade to safe no-ops when the bridge isn't installed yet.

inline void rt_get_technique_timing(mfx::runtime *rt, effect_technique tech,
                                    uint64_t *cpu_ns, uint64_t *gpu_ns)
{
#ifdef MARIUSFX_HOT_DLL
    if (g_host_api && g_host_api->get_technique_timing) {
        unsigned long long c = 0, g = 0;
        g_host_api->get_technique_timing(rt, tech.handle, &c, &g);
        *cpu_ns = c; *gpu_ns = g;
        return;
    }
    (void)rt; (void)tech;
    *cpu_ns = 0;
    *gpu_ns = 0;
#else
    rt->mariusfx_get_technique_timing(tech, cpu_ns, gpu_ns);
#endif
}
inline bool rt_get_performance_mode(mfx::runtime *rt)
{
#ifdef MARIUSFX_HOT_DLL
    if (g_host_api && g_host_api->get_performance_mode)
        return g_host_api->get_performance_mode(rt);
    (void)rt;
    return false;
#else
    return rt->mariusfx_get_performance_mode();
#endif
}
inline void rt_set_performance_mode(mfx::runtime *rt, bool v)
{
#ifdef MARIUSFX_HOT_DLL
    if (g_host_api && g_host_api->set_performance_mode)
        g_host_api->set_performance_mode(rt, v);
    (void)rt; (void)v;
#else
    rt->mariusfx_set_performance_mode(v);
#endif
}
inline void rt_reload_all(mfx::runtime *rt)
{
#ifdef MARIUSFX_HOT_DLL
    if (g_host_api && g_host_api->reload_all) {
        g_host_api->reload_all(rt);
        return;
    }
    (void)rt;
#else
    rt->mariusfx_reload_all();
#endif
}

// (Upstream tab bridge dropped  the Settings panel no longer embeds the
//  host's draw_gui_settings / draw_gui_log / draw_gui_about. If we ever
//  want one of them back, restore the inline host_* helpers here.)

// ── Hot-reload telemetry ───────────────────────────────────────────────────
// Bumped by init() each time the DLL is (re)loaded. Used by the statusbar
// to render a "vN" badge that briefly glows accent-blue right after a
// reload, so the user gets immediate confirmation that a redeploy worked.
int    g_load_count    = 0;
double g_load_time_sec = 0.0;

// ── Workspace mode ─────────────────────────────────────────────────────────
// The right column is a single panel that switches between three modes:
//   PARAMS     per-shader uniform editor (default)
//   STATS      global statistics dashboard
//   SETTINGS   global preferences
// Switching is driven by the toolbar icons (gear / bars). The modes are
// mutually exclusive: clicking a mode that is already active returns to
// PARAMS. No overlay, no z-fighting, no modal  just a tab-style swap.
enum Sheet : int { SHEET_NONE = 0, SHEET_SETTINGS, SHEET_STATISTICS };
int  g_active_sheet = SHEET_NONE;

// Global search applied to BOTH the pipeline and the available-effects
// list. Lives in the toolbar.
char g_toolbar_search[160] = "";

// Available-effects collapsible section (under the pipeline).
bool g_show_available = true;

// ── Custom drag-and-drop reorder ───────────────────────────────────────────
// We bypass ImGui's BeginDragDropSource/Target API entirely. Reasons:
//   1. ImGui's threshold delay before engaging the drag feels sluggish.
//   2. Its tooltip preview is detached from the row body and doesn't
//      respect our theming without a lot of style-stack juggling.
//   3. The OS cursor flips back to Arrow during the drag unless we
//      re-assert Hand every frame from a global handler.
//   4. Drop-on-target requires the mouse to be over a target widget;
//      slightly missing the slot quietly cancels the drag.
//
// Custom flow:
//    Click on the drag-handle column (⠿) of an Active row → drag starts
//     IMMEDIATELY, no movement threshold.
//    While held: pl_draw_pipeline renders a floating "ghost" card that
//     follows the cursor, plus a 2 px insertion line at the target slot.
//    Release: commits reorder via reorder_techniques().
//    Esc or mouse-up outside any row: cancel.
int   g_drag_src        = -1;   // -1 = idle, otherwise g_techs index
float g_drag_grab_dy    = 0.0f; // mouse_y - row_top captured at grab start
int   g_drag_target_idx = -1;   // computed each frame, used NEXT frame to
                                // render the drop-slot at the right spot
                                // (one-frame lag for the visual; commit
                                // uses the live frame_target so it's exact)

// ── Ergonomic state ────────────────────────────────────────────────────────
// Cross-cutting flags that several drawing helpers cooperate on, gathered
// here so the entry-point keyboard handler can drive the UI from a single
// place.
bool   g_focus_search_next  = false; // set by Ctrl+F, consumed by the search input
double g_last_change_time   = -1e9;  // updated whenever a uniform changes  drives the "● Saving" badge
int    g_collapse_all_req   =  0;    // +1 = expand all in the param editor next frame, -1 = collapse all
// Deferred context-menu action for tech rows. The popup is opened on
// right-click in pl_draw_tech_row; the actual mutation is deferred to
// pl_draw_pipeline so we don't mutate g_techs mid-iteration.
enum CtxAction : int {
    CTX_NONE = 0,
    CTX_MOVE_TOP,
    CTX_MOVE_BOTTOM,
    CTX_OPEN_FX,
    CTX_RESET_UNIFORMS,
    CTX_COPY_NAME,
};
int g_ctx_action_idx = -1;
int g_ctx_action     = CTX_NONE;

// ── Layout grid ────────────────────────────────────────────────────────────
// Single source of truth for every dimension the panel uses. If a number
// changes here, every helper picks it up automatically. The goal is:
//    stable rows (height never depends on hover state)
//    predictable horizontal slots (label/control/value/reset)
//    multi-component widgets that obey strict width budgets
namespace L
{
    // Outer chrome.
    constexpr float toolbar_h     = 52.0f;
    constexpr float statusbar_h   = 28.0f;
    constexpr float panel_pad     = 16.0f;  // outer padding for the right panel

    // Pipeline (left column) row.
    constexpr float pl_row_h      = 52.0f;
    constexpr float pl_drag_w     = 16.0f;  // drag-handle slot
    constexpr float pl_toggle_w   = 44.0f;
    constexpr float pl_toggle_h   = 24.0f;
    constexpr float pl_perf_w     = 52.0f;  // reserved for "X.XX ms"

    // Param editor row.
    constexpr float pe_row_h      = 30.0f;
    constexpr float pe_row_gap    =  4.0f;  // vertical breathing room between rows
    constexpr float pe_label_min  = 100.0f;
    constexpr float pe_label_max  = 220.0f;
    constexpr float pe_label_pct  = 0.40f;  // share of body width for labels
    constexpr float pe_value_w    =  56.0f; // single-component readout slot
    constexpr float pe_reset_w    =  18.0f; // ALWAYS reserved (no hover layout shift)
    constexpr float pe_h_gap      =  8.0f;  // gap between label / control / value / reset

    // Header card on top of the param body.
    constexpr float pe_header_h   = 64.0f;
    constexpr float pe_header_gap = 12.0f;  // distance between header and body

    // Body toolbar (Expand / Collapse / Reset all)  NOT in the header card.
    constexpr float pe_bar_h      = 28.0f;
    constexpr float pe_bar_gap    =  8.0f;
}

// ── Legacy tab/pill state  retained as no-ops for now so left-over call
// sites compile while the new pipeline UI takes over. Will be deleted in
// a follow-up cleanup pass.
enum Tab : int { TAB_SHADERS, TAB_SETTINGS, TAB_STATISTICS, TAB_ADDONS, TAB_LOG, TAB_COUNT };
constexpr const char *kTabLabels[TAB_COUNT] = {
    "Shaders", "Settings", "Statistics", "Add-ons", "Log"
};
int g_active_tab  = TAB_SHADERS;
enum Pill : int { PILL_ALL, PILL_ACTIVE, PILL_INACTIVE, PILL_COUNT };
constexpr const char *kPillLabels[PILL_COUNT] = { "All", "Active", "Inactive" };
int g_active_pill = PILL_ALL;
char g_search[128] = "";

// ── Cached technique list (rebuilt every frame, cheap) ─────────────────────
struct TechRow
{
    effect_technique handle;
    char  name[128];
    char  effect_full[260];   // raw effect file path
    char  effect_short[48];   // file name without extension
    bool  enabled;
    bool  hidden;
};
std::vector<TechRow> g_techs;

// Stable selection key  survives technique reorders / reloads. Empty ⇒
// nothing selected ⇒ main panel shows the empty state.
char g_selected_key[256] = "";

// ── Cached uniform list for the selected technique's effect ────────────────
struct UniformRow
{
    effect_uniform_variable handle;
    char        name[96];
    char        label[96];      // ui_label or fallback to name
    char        category[96];   // ui_category, may be empty
    char        tooltip[256];   // ui_tooltip, may be empty
    char        ui_type[24];    // "slider" / "drag" / "combo" / "color" / "list" / "radio" / "button"
    char        ui_text[256];   // ui_text  prose displayed ABOVE the widget
    char        ui_units[24];   // ui_units  suffix appended to numeric readout (e.g. " ms")
    char        items[1024];    // ui_items (combo / list / radio entries, '\0' separated)
    api::format base_type;
    uint32_t    rows, cols, arr;
    float       ui_min[4], ui_max[4], ui_step[4];
    int         ui_spacing;     // ui_spacing  N blank lines BEFORE the widget
    bool        has_range;
    bool        noreset;        // noreset annotation  hide the per-row reset button
    bool        noedit;         // noedit annotation  render as read-only / disabled
};
std::vector<UniformRow> g_uniforms;
char g_uniforms_for_key[256] = ""; // last (effect|tech) we cached uniforms for
char g_uniforms_for_effect[260] = "";

// ── Preset picker state ────────────────────────────────────────────────────
namespace fs = std::filesystem;

// All .ini files discovered in the current preset folder (full paths).
std::vector<std::string> g_preset_files;
// Subdirectories in the current preset folder (full paths).
std::vector<std::string> g_preset_subdirs;
// Folder we last scanned. Refresh triggered when the current preset's
// parent path differs from this.
std::string              g_preset_files_folder;
// Root folder: the initial preset folder. Used as a floor for navigation.
std::string              g_preset_root_folder;
// Navigation history stack for "back" navigation.
std::vector<std::string> g_preset_nav_history;
char                     g_preset_search[160] = "";

bool   g_preset_popup_open  = false;
ImVec2 g_preset_popup_anchor;   // screen-space top-left of the popup
float  g_preset_popup_width = 0.0f;

// Inline "new preset" entry mode.
bool   g_preset_new_mode = false;
char   g_preset_new_buf[128] = "";

// Inline rename for row index N.
int    g_preset_rename_idx = -1;
char   g_preset_rename_buf[128] = "";

// Pending-delete confirmation index.
int    g_preset_delete_confirm = -1;

// ── Dock layout state ──────────────────────────────────────────────────────
// The overlay snaps to either the LEFT or RIGHT side of the viewport.
// Width is user-resizable (custom drag handle on the inner edge); height
// is always full-viewport.
enum DockSide : int { DOCK_LEFT = 0, DOCK_RIGHT = 1 };
int   g_dock_side    = DOCK_LEFT;
float g_window_width = 720.0f;     // last known width, persisted across frames
bool  g_force_size   = true;       // re-apply width on the next configure (after dock toggle / init)
constexpr float kMinWindowWidth  = 480.0f;
constexpr float kMaxWindowFrac   = 0.7f;   // max 70% of viewport width

// ── Settings state (wired to the runtime via host API bridge) ──────────────
// Screenshot config — synced from runtime on first settings panel open.
char  g_ss_path[260]     = {};
char  g_ss_name[128]     = {};
int   g_ss_quality       = 90;
int   g_ss_format        = 1;    // 0=BMP 1=PNG 2=JPEG
bool  g_settings_synced  = false; // true once we've read from runtime

// Hotkey data — each is unsigned int[4]: [0]=VK, [1..3]=modifiers.
unsigned int g_key_overlay[4]    = {};
unsigned int g_key_screenshot[4] = {};
unsigned int g_key_effects[4]    = {};

// Key-capture state: which hotkey is being rebound (-1 = none).
int  g_capturing_key     = -1;   // 0=overlay, 1=screenshot, 2=effects

// Sync settings FROM the runtime (called once on first settings panel open).
void settings_sync_from_runtime(mfx::runtime *rt)
{
    if (g_settings_synced) return;
    g_settings_synced = true;
#ifdef MARIUSFX_HOT_DLL
    if (!g_host_api) return;
    if (g_host_api->get_screenshot_path)
        g_host_api->get_screenshot_path(rt, g_ss_path, sizeof(g_ss_path));
    if (g_host_api->get_screenshot_name)
        g_host_api->get_screenshot_name(rt, g_ss_name, sizeof(g_ss_name));
    if (g_host_api->get_screenshot_quality)
        g_ss_quality = (int)g_host_api->get_screenshot_quality(rt);
    if (g_host_api->get_screenshot_format)
        g_ss_format = (int)g_host_api->get_screenshot_format(rt);
    if (g_host_api->get_overlay_key)
        g_host_api->get_overlay_key(rt, g_key_overlay);
    if (g_host_api->get_screenshot_key)
        g_host_api->get_screenshot_key(rt, g_key_screenshot);
    if (g_host_api->get_effects_key)
        g_host_api->get_effects_key(rt, g_key_effects);
#else
    (void)rt;
#endif
}

// Push a changed setting TO the runtime and persist.
void settings_save(mfx::runtime *rt)
{
#ifdef MARIUSFX_HOT_DLL
    if (!g_host_api) return;
    if (g_host_api->set_screenshot_path)
        g_host_api->set_screenshot_path(rt, g_ss_path);
    if (g_host_api->set_screenshot_name)
        g_host_api->set_screenshot_name(rt, g_ss_name);
    if (g_host_api->set_screenshot_quality)
        g_host_api->set_screenshot_quality(rt, (unsigned int)g_ss_quality);
    if (g_host_api->set_screenshot_format)
        g_host_api->set_screenshot_format(rt, (unsigned int)g_ss_format);
    if (g_host_api->set_overlay_key)
        g_host_api->set_overlay_key(rt, g_key_overlay);
    if (g_host_api->set_screenshot_key)
        g_host_api->set_screenshot_key(rt, g_key_screenshot);
    if (g_host_api->set_effects_key)
        g_host_api->set_effects_key(rt, g_key_effects);
    if (g_host_api->save_config)
        g_host_api->save_config(rt);
#else
    (void)rt;
#endif
}

// Convert a VK code to a human-readable name.
void vk_to_name(unsigned int vk, char *buf, unsigned int sz)
{
    if (vk == 0)                          { snprintf(buf, sz, "None"); return; }
    if (vk >= VK_F1 && vk <= VK_F24)     { snprintf(buf, sz, "F%u", vk - VK_F1 + 1); return; }
    if (vk >= 'A' && vk <= 'Z')          { snprintf(buf, sz, "%c", (char)vk); return; }
    if (vk >= '0' && vk <= '9')          { snprintf(buf, sz, "%c", (char)vk); return; }
    switch (vk) {
        case VK_HOME:     snprintf(buf, sz, "Home"); break;
        case VK_END:      snprintf(buf, sz, "End"); break;
        case VK_INSERT:   snprintf(buf, sz, "Insert"); break;
        case VK_DELETE:   snprintf(buf, sz, "Delete"); break;
        case VK_SPACE:    snprintf(buf, sz, "Space"); break;
        case VK_RETURN:   snprintf(buf, sz, "Enter"); break;
        case VK_ESCAPE:   snprintf(buf, sz, "Escape"); break;
        case VK_TAB:      snprintf(buf, sz, "Tab"); break;
        case VK_BACK:     snprintf(buf, sz, "Backspace"); break;
        case VK_PRIOR:    snprintf(buf, sz, "PgUp"); break;
        case VK_NEXT:     snprintf(buf, sz, "PgDown"); break;
        case VK_LEFT:     snprintf(buf, sz, "Left"); break;
        case VK_RIGHT:    snprintf(buf, sz, "Right"); break;
        case VK_UP:       snprintf(buf, sz, "Up"); break;
        case VK_DOWN:     snprintf(buf, sz, "Down"); break;
        case VK_SNAPSHOT: snprintf(buf, sz, "PrtScn"); break;
        case VK_PAUSE:    snprintf(buf, sz, "Pause"); break;
        case VK_NUMLOCK:  snprintf(buf, sz, "NumLock"); break;
        case VK_CAPITAL:  snprintf(buf, sz, "CapsLock"); break;
        case VK_SCROLL:   snprintf(buf, sz, "ScrLock"); break;
        case VK_OEM_PLUS: snprintf(buf, sz, "+"); break;
        case VK_OEM_MINUS:snprintf(buf, sz, "-"); break;
        case VK_OEM_COMMA:snprintf(buf, sz, ","); break;
        case VK_OEM_PERIOD:snprintf(buf, sz, "."); break;
        default:          snprintf(buf, sz, "0x%02X", vk); break;
    }
}

// Build a display string for key_data[4] (VK + up to 3 modifiers).
void key_data_to_string(const unsigned int key_data[4], char *buf, unsigned int sz)
{
    buf[0] = '\0';
    char part[32];
    for (int i = 3; i >= 1; --i) {
        if (key_data[i] == 0) continue;
        vk_to_name(key_data[i], part, sizeof(part));
        size_t len = strlen(buf);
        snprintf(buf + len, sz - (unsigned int)len, "%s + ", part);
    }
    vk_to_name(key_data[0], part, sizeof(part));
    size_t len = strlen(buf);
    snprintf(buf + len, sz - (unsigned int)len, "%s", part);
}

// ── Misc ───────────────────────────────────────────────────────────────────
void colored_text(ImU32 c, const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::to_vec4(c));
    ImGui::TextV(fmt, a);
    ImGui::PopStyleColor();
    va_end(a);
}

bool icase_contains(const char *hay, const char *needle)
{
    if (!needle || !needle[0]) return true;
    for (const char *h = hay; *h; ++h)
    {
        const char *n = needle, *hh = h;
        while (*n && *hh)
        {
            char a = *hh, b = *n;
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            ++hh; ++n;
        }
        if (!*n) return true;
    }
    return false;
}

void prettify_effect_name(const char *src, char *dst, size_t dst_size)
{
    const char *base = src;
    for (const char *p = src; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    size_t i = 0;
    while (base[i] && base[i] != '.' && i + 1 < dst_size) { dst[i] = base[i]; ++i; }
    dst[i] = 0;
}

void make_key(char *out, size_t sz, const char *eff, const char *tech)
{
    snprintf(out, sz, "%s|%s", eff ? eff : "", tech ? tech : "");
}

void copy_str(char *dst, size_t dst_size, const char *src)
{
    if (!src) { if (dst_size) dst[0] = 0; return; }
    size_t i = 0;
    while (src[i] && i + 1 < dst_size) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

// ── Preset helpers ─────────────────────────────────────────────────────────
// Extract the preset display name (file stem) from a full path.
std::string preset_basename(const std::string &full)
{
    if (full.empty()) return {};
    const size_t slash = full.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? full : full.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name.erase(dot);
    return name;
}

// (Re)build g_preset_files and g_preset_subdirs for the given folder.
// Sorted case-insensitively by file stem / folder name.
void refresh_preset_files(const std::string &folder)
{
    g_preset_files.clear();
    g_preset_subdirs.clear();
    g_preset_files_folder = folder;
    if (folder.empty()) return;
    std::error_code ec;
    if (!fs::exists(folder, ec) || !fs::is_directory(folder, ec)) return;
    for (const auto &e : fs::directory_iterator(folder, ec))
    {
        if (ec) break;
        if (e.is_directory(ec)) {
            g_preset_subdirs.push_back(e.path().string());
            continue;
        }
        if (!e.is_regular_file(ec)) continue;
        const fs::path &p = e.path();
        std::string ext = p.extension().string();
        for (char &c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".ini") continue;
        g_preset_files.push_back(p.string());
    }
    auto icase_sort = [](const std::string &a, const std::string &b) {
        std::string an = a, bn = b;
        // Extract last component for comparison
        auto last_comp = [](const std::string &s) -> std::string {
            size_t pos = s.find_last_of("/\\");
            return (pos == std::string::npos) ? s : s.substr(pos + 1);
        };
        an = last_comp(an); bn = last_comp(bn);
        for (char &c : an) c = (char)std::tolower((unsigned char)c);
        for (char &c : bn) c = (char)std::tolower((unsigned char)c);
        return an < bn;
    };
    std::sort(g_preset_subdirs.begin(), g_preset_subdirs.end(), icase_sort);
    std::sort(g_preset_files.begin(), g_preset_files.end(),
              [](const std::string &a, const std::string &b) {
                  std::string an = preset_basename(a);
                  std::string bn = preset_basename(b);
                  for (char &c : an) c = (char)std::tolower((unsigned char)c);
                  for (char &c : bn) c = (char)std::tolower((unsigned char)c);
                  return an < bn;
              });
}

// Navigate into a subfolder, pushing current folder onto the history stack.
void navigate_preset_folder(const std::string &new_folder)
{
    if (!g_preset_files_folder.empty())
        g_preset_nav_history.push_back(g_preset_files_folder);
    g_preset_search[0] = '\0';
    g_preset_rename_idx = -1;
    g_preset_delete_confirm = -1;
    refresh_preset_files(new_folder);
}

// Navigate back to the previous folder in history.
bool navigate_preset_back()
{
    if (g_preset_nav_history.empty()) return false;
    std::string prev = g_preset_nav_history.back();
    g_preset_nav_history.pop_back();
    g_preset_search[0] = '\0';
    g_preset_rename_idx = -1;
    g_preset_delete_confirm = -1;
    refresh_preset_files(prev);
    return true;
}

// Get folder display name (last path component).
std::string folder_display_name(const std::string &full)
{
    if (full.empty()) return {};
    const size_t slash = full.find_last_of("/\\");
    return (slash == std::string::npos) ? full : full.substr(slash + 1);
}

// Read the runtime's current preset path into a std::string.
std::string current_preset_path(mfx::runtime *rt)
{
    char buf[1024] = ""; size_t s = sizeof(buf);
    rt->get_current_preset_path(buf, &s);
    buf[sizeof(buf) - 1] = 0;
    return std::string(buf);
}

// Read a string annotation safely. Returns true if present and non-empty.
bool read_str_annot_uniform(effect_runtime *r, effect_uniform_variable v,
                            const char *name, char *out, size_t out_size)
{
    size_t s = out_size;
    if (!r->get_annotation_string_from_uniform_variable(v, name, out, &s))
        { if (out_size) out[0] = 0; return false; }
    return out[0] != 0;
}

bool read_str_annot_tech(effect_runtime *r, effect_technique v,
                         const char *name, char *out, size_t out_size)
{
    size_t s = out_size;
    if (!r->get_annotation_string_from_technique(v, name, out, &s))
        { if (out_size) out[0] = 0; return false; }
    return out[0] != 0;
}

// ── Group-active helper ────────────────────────────────────────────────────
// Reorders the pipeline so every currently-enabled technique appears at
// the top of the list, preserving the relative order within both the
// enabled and disabled groups. Triggered from the dedicated toolbar
// button next to the filter chips (the user asked for an explicit action
// rather than auto-promote on every toggle).
void promote_all_enabled(mfx::runtime *rt)
{
    if (g_techs.empty()) return;
    std::vector<effect_technique> order;
    order.reserve(g_techs.size());
    // Pass 1: enabled techs, in current pipeline order.
    for (const auto &t : g_techs) if (t.enabled)  order.push_back(t.handle);
    // Pass 2: disabled techs, in current pipeline order.
    for (const auto &t : g_techs) if (!t.enabled) order.push_back(t.handle);
    rt->reorder_techniques(order.size(), order.data());
}

// ── Data refresh ───────────────────────────────────────────────────────────
void refresh_tech_list(mfx::runtime *rt)
{
    g_techs.clear();
    rt->enumerate_techniques(nullptr, [](effect_runtime *r, effect_technique tech, void *) {
        TechRow row{};
        row.handle = tech;

        size_t n = sizeof(row.name);
        r->get_technique_name(tech, row.name, &n);

        n = sizeof(row.effect_full);
        r->get_technique_effect_name(tech, row.effect_full, &n);
        prettify_effect_name(row.effect_full, row.effect_short, sizeof(row.effect_short));

        row.enabled = r->get_technique_state(tech);

        // hidden annotation  true if technique should be invisible.
        bool hidden_v = false;
        size_t cnt = 1;
        (void)cnt; // some overloads ignore count
        r->get_annotation_bool_from_technique(tech, "hidden", &hidden_v, 1);
        row.hidden = hidden_v;

        g_techs.push_back(row);
    }, nullptr);
}

void refresh_uniform_list(mfx::runtime *rt, const char *effect_name)
{
    g_uniforms.clear();
    if (!effect_name || !effect_name[0]) return;

    // Debug counters to diagnose missing parameters.
    static int s_total_enumerated = 0;
    static int s_filtered_hidden = 0;
    static int s_filtered_source = 0;
    static int s_filtered_sep = 0;
    s_total_enumerated = s_filtered_hidden = s_filtered_source = s_filtered_sep = 0;

    // Filtering follows ReShade's reference draw_variable_editor (see
    // source/runtime_gui.cpp): we only hide uniforms that are explicitly
    // flagged hidden=1 in their annotations or that the runtime backs
    // with a special source (TIMER, FRAMECOUNT, OVERLAY_*, ...). Every
    // other uniform is displayed in source order, with an ImGui widget
    // selected from ui_type and base_type.
    //
    // Specifically NOT filtered any more:
    //   * names starting with "__" (MartyMods/Sirius/Solaris use them
    //     for real, user-facing parameters)
    //   * blank ui_label (we fall back to the variable name)
    //   * sepN / __PADn (rendered as decorative rows; shader authors
    //     control whether they collapse via ui_category instead)
    rt->enumerate_uniform_variables(effect_name,
        [](effect_runtime *r, effect_uniform_variable var, void *) {
            s_total_enumerated++;
            
            UniformRow u{};
            u.handle = var;

            size_t n = sizeof(u.name);
            r->get_uniform_variable_name(var, u.name, &n);
            r->get_uniform_variable_type(var, &u.base_type, &u.rows, &u.cols, &u.arr);

            // ── Hard filters (mirror ReShade) ───────────────────────────
            // 1. hidden=1 annotation set by the shader author.
            int hidden_v = 0;
            if (r->get_annotation_int_from_uniform_variable(var, "hidden", &hidden_v, 1) && hidden_v != 0) {
                s_filtered_hidden++;
                return;
            }
            // 2. Special-source uniforms (TIMER / FRAMECOUNT / OVERLAY_*).
            char source[32] = "";
            read_str_annot_uniform(r, var, "source", source, sizeof(source));
            if (source[0]) {
                s_filtered_source++;
                return;
            }
            // 3. Decorative spacer uniforms (sep0, sep1, ... sep99). QuantV
            //    and other packs use these as visual separators; they have no
            //    actual value to edit and clutter the parameter list.
            {
                const char *n = u.name;
                if ((n[0] == 's' || n[0] == 'S') &&
                    (n[1] == 'e' || n[1] == 'E') &&
                    (n[2] == 'p' || n[2] == 'P'))
                {
                    const char *p = n + 3;
                    bool all_digits = (*p != '\0');
                    while (*p) {
                        if (*p < '0' || *p > '9') { all_digits = false; break; }
                        ++p;
                    }
                    if (all_digits) {
                        s_filtered_sep++;
                        return;
                    }
                }
            }

            // ── Annotations driving widget selection / dressing ─────────
            read_str_annot_uniform(r, var, "ui_type",     u.ui_type,  sizeof(u.ui_type));
            read_str_annot_uniform(r, var, "ui_label",    u.label,    sizeof(u.label));
            read_str_annot_uniform(r, var, "ui_category", u.category, sizeof(u.category));
            read_str_annot_uniform(r, var, "ui_tooltip",  u.tooltip,  sizeof(u.tooltip));
            read_str_annot_uniform(r, var, "ui_text",     u.ui_text,  sizeof(u.ui_text));
            read_str_annot_uniform(r, var, "ui_units",    u.ui_units, sizeof(u.ui_units));
            read_str_annot_uniform(r, var, "ui_items",    u.items,    sizeof(u.items));

            // Label fallback  ReShade's `if (label.empty()) label = variable.name;`.
            // We also treat all-whitespace labels (QuantV's " " spacers) as empty
            // so the uniform name surfaces instead of an invisible row.
            {
                bool blank = (u.label[0] == '\0');
                if (!blank) {
                    bool all_ws = true;
                    for (const char *p = u.label; *p; ++p)
                        if ((unsigned char)*p > ' ') { all_ws = false; break; }
                    blank = all_ws;
                }
                if (blank) copy_str(u.label, sizeof(u.label), u.name);
            }

            // ui_items stores entry separators as the literal characters
            // "\\0"  rewrite to actual null bytes so ImGui::Combo and the
            // list/radio walkers see proper '\0'-terminated tokens.
            for (size_t i = 0; i + 1 < sizeof(u.items) && u.items[i]; ++i)
                if (u.items[i] == '\\' && u.items[i + 1] == '0')
                    { u.items[i] = 0; memmove(u.items + i + 1, u.items + i + 2, sizeof(u.items) - i - 2); }

            // Numeric annotations  spacing, behaviour flags.
            int spacing = 0;
            (void)r->get_annotation_int_from_uniform_variable(var, "ui_spacing", &spacing, 1);
            u.ui_spacing = spacing;
            int flag = 0;
            u.noreset = r->get_annotation_int_from_uniform_variable(var, "noreset", &flag, 1) && flag != 0;
            flag = 0;
            u.noedit  = r->get_annotation_int_from_uniform_variable(var, "noedit",  &flag, 1) && flag != 0;

            // Numeric ranges. Try the type-correct annotation first; for
            // int/uint the runtime stores them as ints natively, but we
            // upcast everything to float so the slider widget can use a
            // single API.
            const uint32_t comps = u.rows ? u.rows : 1;
            if (u.base_type == api::format::r32_float || u.base_type == api::format::r16_float)
            {
                u.has_range  = r->get_annotation_float_from_uniform_variable(var, "ui_min",  u.ui_min,  comps);
                u.has_range &= r->get_annotation_float_from_uniform_variable(var, "ui_max",  u.ui_max,  comps);
                r->get_annotation_float_from_uniform_variable(var, "ui_step", u.ui_step, comps);
            }
            else
            {
                int32_t imn[4]={0,0,0,0}, imx[4]={0,0,0,0}, ist[4]={0,0,0,0};
                u.has_range  = r->get_annotation_int_from_uniform_variable(var, "ui_min",  imn, comps);
                u.has_range &= r->get_annotation_int_from_uniform_variable(var, "ui_max",  imx, comps);
                r->get_annotation_int_from_uniform_variable(var, "ui_step", ist, comps);
                for (uint32_t k = 0; k < 4; ++k)
                {
                    u.ui_min[k]  = static_cast<float>(imn[k]);
                    u.ui_max[k]  = static_cast<float>(imx[k]);
                    u.ui_step[k] = static_cast<float>(ist[k]);
                }
            }

            g_uniforms.push_back(u);
        }, nullptr);

    // Debug output (will appear in window title via ImGui::Text in a debug panel).
    // TODO: Remove after diagnosing missing parameters.
    char debug_buf[256];
    snprintf(debug_buf, sizeof(debug_buf),
             "[DEBUG] %s: %d total, %d hidden, %d source, %d sep -> %d visible",
             effect_name, s_total_enumerated, s_filtered_hidden,
             s_filtered_source, s_filtered_sep, (int)g_uniforms.size());
    copy_str(g_uniforms_for_effect, sizeof(g_uniforms_for_effect), debug_buf);
}

// Look up the currently selected TechRow by stable key. Returns -1 if not found.
int find_selected_index()
{
    if (!g_selected_key[0]) return -1;
    char key[256];
    for (int i = 0; i < (int)g_techs.size(); ++i)
    {
        make_key(key, sizeof(key), g_techs[i].effect_full, g_techs[i].name);
        if (std::strcmp(key, g_selected_key) == 0) return i;
    }
    return -1;
}

// ── Frametime ring buffer (for the Statistics tab graph) ───────────────────
constexpr size_t FT_SAMPLES = 240;
float g_ft_buf[FT_SAMPLES] = {};
size_t g_ft_pos = 0;
bool   g_ft_filled = false;

void push_frametime_sample()
{
    const float fps = ImGui::GetIO().Framerate;
    g_ft_buf[g_ft_pos] = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
    g_ft_pos = (g_ft_pos + 1) % FT_SAMPLES;
    if (g_ft_pos == 0) g_ft_filled = true;
}

float ft_ring_get(int i)
{
    // Returns sample i in chronological order (0 = oldest, FT_SAMPLES-1 = newest).
    const size_t base = g_ft_filled ? g_ft_pos : 0;
    return g_ft_buf[(base + (size_t)i) % FT_SAMPLES];
}

// ── Custom widgets ─────────────────────────────────────────────────────────
//
// Slider design notes
// -------------------
// One widget, three behaviours:
//
//   Position-based by default  thumb tracks the cursor, snappy.
//   Shift+drag  delta-based, 0.1 sensitivity, for precision.
//   Ctrl+drag  delta-based, 4 sensitivity, for sweep.
//   Double-click or right-click  emits SLIDER_RESET.
//   Ctrl+click (no drag)  swaps to inline text input; Enter / focus-out
//    commits, Esc cancels.
//
// Return value
// ------------
//   SLIDER_NONE      nothing happened this frame
//   SLIDER_CHANGED   *value was updated; caller should push to runtime
//   SLIDER_RESET     caller should call reset_uniform_value()
//
// Visuals
// -------
//   ──────────────────         track ── 4 px, fill accent until thumb
//        ╲ thumb circle 12 px, halo 16 px on hover
enum SliderResult : int { SLIDER_NONE = 0, SLIDER_CHANGED = 1, SLIDER_RESET = 2 };

// Persistent "this slider is in numeric-input mode" state. Only one slider
// at a time can be in input mode (we'd never hit that limit anyway).
char g_slider_input_id [220] = "";
char g_slider_input_buf[ 64] = "";

// Format a float for the input box without scientific noise where avoidable.
inline void slider_format_value(char *out, size_t cap, float v)
{
    snprintf(out, cap, "%.4g", v);
}

SliderResult mfx_slider_float_ex(const char *id, float w, float h,
                                 float vmin, float vmax, float step,
                                 float *value, bool show_value = true)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + w, a.y + h);
    const ImGuiIO &io = ImGui::GetIO();

    // ── Numeric input mode ─────────────────────────────────────────
    // Triggered by double-click. Drawn in place of the slider so the
    // cursor lands directly in the field.
    if (std::strcmp(id, g_slider_input_id) == 0)
    {
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y - 2.0f));
        ImGui::SetNextItemWidth(w);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        to_vec4(col::bg_input));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, to_vec4(col::bg_input));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  to_vec4(col::bg_input));
        ImGui::PushStyleColor(ImGuiCol_Text,           to_vec4(col::text_primary));
        ImGui::PushStyleColor(ImGuiCol_Border,         to_vec4(col::border_accent));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   4.0f);
        // Auto-focus on the first frame the input opens.
        if (!ImGui::IsAnyItemActive())
            ImGui::SetKeyboardFocusHere();
        const bool committed =
            ImGui::InputText(id, g_slider_input_buf, sizeof(g_slider_input_buf),
                             ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll |
                             ImGuiInputTextFlags_CharsScientific);
        const bool deactivated = ImGui::IsItemDeactivated();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);

        SliderResult result = SLIDER_NONE;
        if (committed) {
            const float nv = (float)atof(g_slider_input_buf); // No clamp: allow exceeding shader limits via manual input
            if (nv != *value) { *value = nv; result = SLIDER_CHANGED; }
            g_slider_input_id[0] = '\0';
        }
        else if (deactivated) {
            // Focus lost without commit  treat as cancel.
            g_slider_input_id[0] = '\0';
        }
        return result;
    }

    // ── Normal slider mode ─────────────────────────────────────────
    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool active     = ImGui::IsItemActive();
    const bool hov        = ImGui::IsItemHovered() || active;
    const bool dbl        = ImGui::IsItemHovered() &&
                            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    const bool right_clk  = ImGui::IsItemHovered() &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    // Reset shortcut  right-click only.
    if (right_clk) return SLIDER_RESET;

    // Double-click  switch to numeric input mode for manual value entry.
    if (dbl)
    {
        copy_str(g_slider_input_id, sizeof(g_slider_input_id), id);
        slider_format_value(g_slider_input_buf, sizeof(g_slider_input_buf), *value);
        ImGui::ClearActiveID();
    }

    SliderResult result = SLIDER_NONE;
    if (active && vmax > vmin)
    {
        float nv = *value;
        if (io.KeyShift || io.KeyCtrl) {
            // Delta-based mode (precision / coarse).
            const float range = vmax - vmin;
            const float speed = (range / w) * (io.KeyShift ? 0.1f : 4.0f);
            nv += io.MouseDelta.x * speed;
        } else {
            // Position-based: thumb chases the cursor.
            const float t = ImClamp((io.MousePos.x - a.x) / w, 0.0f, 1.0f);
            nv = vmin + t * (vmax - vmin);
        }
        if (step > 0.0f)
            nv = vmin + std::round((nv - vmin) / step) * step;
        nv = ImClamp(nv, vmin, vmax);
        if (nv != *value) { *value = nv; result = SLIDER_CHANGED; }
    }

    // ── Visuals ─────────────────────────────────────────────────────
    // Flat bar slider: full-height rounded bar with accent fill and
    // value text centered inside. Consistent across single/multi-comp.
    const float rad = h * 0.35f;

    // Background bar
    dl->AddRectFilled(a, b, col::bg_input, rad);

    const float t = (vmax > vmin)
        ? ImClamp((*value - vmin) / (vmax - vmin), 0.0f, 1.0f) : 0.0f;
    const float fx = a.x + w * t;

    // Filled portion
    if (fx > a.x + 2.0f)
        dl->AddRectFilled(a, ImVec2(fx, b.y),
                          active ? col::accent_strong : (hov ? col::accent_hover : col::accent), rad);

    // Subtle border on hover
    if (hov)
        dl->AddRect(a, b, col::border_accent, rad, 0, 1.0f);

    // Value text centered inside the bar
    if (show_value) {
        char val_str[32];
        snprintf(val_str, sizeof(val_str), "%.3g", *value);
        const ImVec2 txt_size = ImGui::CalcTextSize(val_str);
        const float txt_x = a.x + (w - txt_size.x) * 0.5f;
        const float txt_y = a.y + (h - txt_size.y) * 0.5f;
        dl->AddText(ImVec2(txt_x, txt_y), IM_COL32(255, 255, 255, 220), val_str);
    }

    if (hov && !active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    return result;
}

// Compatibility wrappers  most callers still pass no step.
bool mfx_slider_float(const char *id, float w, float h,
                      float vmin, float vmax, float *value)
{
    return mfx_slider_float_ex(id, w, h, vmin, vmax, 0.0f, value) == SLIDER_CHANGED;
}

bool mfx_slider_int(const char *id, float w, float h,
                    int vmin, int vmax, int *value)
{
    float f = static_cast<float>(*value);
    const SliderResult r = mfx_slider_float_ex(id, w, h,
                                               static_cast<float>(vmin),
                                               static_cast<float>(vmax),
                                               1.0f, &f);
    if (r == SLIDER_CHANGED) {
        int nv = (int)(f + (f >= 0.0f ? 0.5f : -0.5f));
        if (nv < vmin) nv = vmin;
        if (nv > vmax) nv = vmax;
        if (nv != *value) { *value = nv; return true; }
    }
    return false;
}

// Extended int-slider variant exposing reset events too  used by draw_uniform.
SliderResult mfx_slider_int_ex(const char *id, float w, float h,
                               int vmin, int vmax, int *value)
{
    float f = static_cast<float>(*value);
    const SliderResult r = mfx_slider_float_ex(id, w, h,
                                               static_cast<float>(vmin),
                                               static_cast<float>(vmax),
                                               1.0f, &f);
    if (r == SLIDER_CHANGED) {
        int nv = (int)(f + (f >= 0.0f ? 0.5f : -0.5f));
        if (nv < vmin) nv = vmin;
        if (nv > vmax) nv = vmax;
        if (nv != *value) { *value = nv; return SLIDER_CHANGED; }
        return SLIDER_NONE;
    }
    return r;
}

// Multi-component float slider  N stacked custom sliders inside a fixed
// width budget. Each mini-slider gets ⌊w/n⌋ minus inter-track gap. Two
// affordances make all N values legible at a glance:
//
//    A tiny component letter ("X" / "Y" / "Z" / "W") sits at the LEFT
//     of each mini-track, in dim accent. The user immediately knows
//     which slider is which without having to read a separate label.
//    The current value is rendered inline. When the thumb sits in the
//     LEFT half of its track, the value is drawn at the RIGHT end; when
//     the thumb sits in the RIGHT half, the value flips to the LEFT
//     end. The text therefore never disappears behind the thumb.
//
// Returns the "strongest" result across components
// (RESET wins over CHANGED wins over NONE).
SliderResult mfx_slider_floatN_ex(const char *base_id, int n,
                                  float w, float h,
                                  float vmin, float vmax, float step,
                                  float *values)
{
    using namespace theme;
    if (n < 1) n = 1;
    if (n > 4) n = 4;

    const float gap        = 6.0f;
    const float each       = (w - gap * (float)(n - 1)) / (float)n;
    const float track_w    = each;
    if (track_w < 40.0f) {
        // Not enough room for N sliders  fall back to a single component
        // slider and flag the truncation in a tooltip.
        char id[256]; snprintf(id, sizeof(id), "%s_0", base_id);
        const SliderResult r = mfx_slider_float_ex(id, w, h, vmin, vmax, step, values);
        if (ImGui::IsItemHovered() && n > 1)
            ImGui::SetTooltip("Component 0 of %d shown  widen the panel "
                              "to see all components.", n);
        return r;
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    SliderResult result = SLIDER_NONE;

    for (int i = 0; i < n; ++i) {
        const float x  = origin.x + i * (each + gap);
        const float tx = x;  // track left edge (no letter offset)

        // The mini-slider itself (value display disabled, we render it separately).
        char id[256]; snprintf(id, sizeof(id), "%s_%d", base_id, i);
        ImGui::SetCursorScreenPos(ImVec2(tx, origin.y));
        const SliderResult r =
            mfx_slider_float_ex(id, track_w, h, vmin, vmax, step, &values[i], false);
        if (r > result) result = r;

        // Inline value rendered AFTER the slider, so it sits on top.
        // Flipped left/right based on thumb position to avoid occlusion.
        char vbuf[24]; snprintf(vbuf, sizeof(vbuf), "%.2f", values[i]);
        const float vw   = ImGui::CalcTextSize(vbuf).x;
        const float vy   = origin.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
        const float t    = (vmax > vmin)
            ? ImClamp((values[i] - vmin) / (vmax - vmin), 0.0f, 1.0f) : 0.0f;
        const float vx_right = tx + track_w - vw - 6.0f;
        const float vx_left  = tx + 6.0f;
        // Thumb in left half → value renders on the right, and vice-versa.
        const float vx = (t < 0.5f) ? vx_right : vx_left;
        ImGui::GetWindowDrawList()->AddText(ImVec2(vx, vy),
                                            col::text_primary, vbuf);
    }
    return result;
}

SliderResult mfx_slider_intN_ex(const char *base_id, int n,
                                float w, float h,
                                int vmin, int vmax, int *values)
{
    float f[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < n; ++i) f[i] = (float)values[i];
    const SliderResult r = mfx_slider_floatN_ex(base_id, n, w, h,
                                                (float)vmin, (float)vmax,
                                                1.0f, f);
    if (r == SLIDER_CHANGED) {
        bool any = false;
        for (int i = 0; i < n; ++i) {
            int nv = (int)(f[i] + (f[i] >= 0.0f ? 0.5f : -0.5f));
            if (nv < vmin) nv = vmin;
            if (nv > vmax) nv = vmax;
            if (nv != values[i]) { values[i] = nv; any = true; }
        }
        return any ? SLIDER_CHANGED : SLIDER_NONE;
    }
    return r;
}

// Reset glyph drawn manually (refresh-arrow-style circle with break).
void draw_reset_glyph(ImVec2 c, float r, ImU32 color)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->PathClear();
    dl->PathArcTo(c, r, IM_PI * 0.35f, IM_PI * 1.85f, 16);
    dl->PathStroke(color, ImDrawFlags_None, 1.5f);
    // Arrow head at the end of the arc.
    const float ang = IM_PI * 0.35f;
    const ImVec2 tip(c.x + ImCos(ang) * r, c.y + ImSin(ang) * r);
    dl->AddLine(tip, ImVec2(tip.x - 3.0f, tip.y - 1.0f), color, 1.5f);
    dl->AddLine(tip, ImVec2(tip.x + 1.0f, tip.y - 3.5f), color, 1.5f);
}

// Pill toggle button used in the sidebar filter strip.
bool filter_pill(const char *label, bool active)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float pw = ts.x + 18.0f;
    const float ph = ts.y + 8.0f;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b = ImVec2(a.x + pw, a.y + ph);

    ImGui::SetCursorScreenPos(a);
    ImGui::InvisibleButton(label, ImVec2(pw, ph));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();

    const ImU32 bg = active ? col::accent_subtle
                            : hovered ? col::bg_card_hover
                                      : IM_COL32(0, 0, 0, 0);
    const ImU32 br = active ? col::border_accent
                            : col::border_default;
    const ImU32 fg = active ? col::accent_strong
                            : hovered ? col::text_secondary
                                      : col::text_dimmer;
    dl->AddRectFilled(a, b, bg, size::radius_pill);
    dl->AddRect      (a, b, br, size::radius_pill);
    dl->AddText(ImVec2(a.x + 9.0f, a.y + (ph - ts.y) * 0.5f), fg, label);
    return clicked;
}

// Animated toggle switch (true → accent, false → muted). Returns true on click.
bool toggle_switch(const char *id, bool value, float w = 36.0f, float h = 20.0f)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b = ImVec2(a.x + w, a.y + h);

    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool clicked = ImGui::IsItemClicked();

    const ImU32 bg = value ? col::accent : col::bg_input;
    dl->AddRectFilled(a, b, bg, h * 0.5f);
    dl->AddRect      (a, b, value ? col::accent_hover : col::border_default, h * 0.5f);

    const float r = (h - 6.0f) * 0.5f;
    const float cx = value ? (b.x - r - 3.0f) : (a.x + r + 3.0f);
    dl->AddCircleFilled(ImVec2(cx, a.y + h * 0.5f), r,
                        IM_COL32(0xFF, 0xFF, 0xFF, value ? 0xFF : 0xC0));
    return clicked;
}

// Sidebar shader row. Returns 0 = no action, 1 = toggle, 2 = select.
int shader_row(const TechRow &row, bool selected, float row_w)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float row_h = 30.0f;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b = ImVec2(a.x + row_w, a.y + row_h);

    // Hover/select background.
    const ImVec2 mp = ImGui::GetIO().MousePos;
    const bool hov = (mp.x >= a.x && mp.x <= b.x && mp.y >= a.y && mp.y <= b.y);
    if (selected) {
        dl->AddRectFilled(a, b, col::accent_subtle, size::radius_shader_row);
        dl->AddRect      (a, b, col::border_accent, size::radius_shader_row);
    } else if (hov) {
        dl->AddRectFilled(a, b, col::bg_card_hover, size::radius_shader_row);
    }

    // Left accent strip  3px wide, only on enabled techniques. Gives a
    // clear at-a-glance signal of which shaders are live.
    if (row.enabled)
    {
        dl->AddRectFilled(ImVec2(a.x, a.y + 4.0f),
                          ImVec2(a.x + 3.0f, b.y - 4.0f),
                          col::accent, 1.5f);
    }

    // Toggle box.
    const float box = 14.0f;
    const ImVec2 ba(a.x + 10.0f, a.y + (row_h - box) * 0.5f);
    const ImVec2 bb(ba.x + box,  ba.y + box);
    dl->AddRectFilled(ba, bb, row.enabled ? col::accent : IM_COL32(0, 0, 0, 0), 4.0f);
    dl->AddRect      (ba, bb, row.enabled ? col::accent : col::border_subtle,  4.0f, 0, 1.5f);
    if (row.enabled)
    {
        dl->AddLine(ImVec2(ba.x + 3.0f,  ba.y + 7.0f),
                    ImVec2(ba.x + 6.0f,  ba.y + 10.5f),
                    IM_COL32(0xFF, 0xFF, 0xFF, 0xFF), 1.6f);
        dl->AddLine(ImVec2(ba.x + 6.0f,  ba.y + 10.5f),
                    ImVec2(ba.x + 11.0f, ba.y + 4.0f),
                    IM_COL32(0xFF, 0xFF, 0xFF, 0xFF), 1.6f);
    }

    // Tag (effect short name)  right-aligned.
    const float tw = ImGui::CalcTextSize(row.effect_short).x + 10.0f;
    const float th = 16.0f;
    const ImVec2 ta(b.x - tw - 8.0f, a.y + (row_h - th) * 0.5f);
    const ImVec2 tb(ta.x + tw, ta.y + th);
    dl->AddRectFilled(ta, tb, row.enabled ? col::accent_subtle : col::bg_card, 4.0f);
    dl->AddText(ImVec2(ta.x + 5.0f, ta.y + (th - ImGui::GetTextLineHeight()) * 0.5f),
                row.enabled ? col::accent_strong : col::text_dimmer, row.effect_short);

    // Name  clipped to fit between toggle and tag.
    const float name_x  = a.x + 32.0f;
    const float name_w  = (ta.x - 8.0f) - name_x;
    if (name_w > 8.0f)
    {
        const ImU32 nc = selected ? col::accent_strong
                                  : row.enabled ? col::text_secondary
                                                : col::text_dim;
        ImVec4 clip(name_x, a.y, name_x + name_w, b.y);
        dl->PushClipRect(ImVec2(clip.x, clip.y), ImVec2(clip.z, clip.w), true);
        dl->AddText(ImVec2(name_x, a.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                    nc, row.name);
        dl->PopClipRect();
    }

    // Click split: left 28 px = toggle, rest = select.
    char id_buf[160]; snprintf(id_buf, sizeof(id_buf), "##row_%s_%s", row.effect_short, row.name);
    ImGui::SetCursorScreenPos(a);
    ImGui::InvisibleButton(id_buf, ImVec2(row_w, row_h));
    if (ImGui::IsItemClicked())
    {
        const bool on_toggle = (mp.x - a.x) < 28.0f;
        return on_toggle ? 1 : 2;
    }
    return 0;
}

// ── Titlebar ───────────────────────────────────────────────────────────────

// ── Sidebar (Shaders tab) ──────────────────────────────────────────────────

void draw_sidebar_idle(ImVec2 origin, float width, float height, const char *tab_label)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), col::bg_sidebar);
    dl->AddLine(ImVec2(origin.x + width, origin.y),
                ImVec2(origin.x + width, origin.y + height),
                col::border_default);

    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##mfx_side_idle", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy(ImVec2(0, 18));
    ImGui::Indent(14);
    colored_text(col::text_disabled, "%s", tab_label);
    ImGui::Dummy(ImVec2(0, 8));
    colored_text(col::text_dimmer, "The sidebar is");
    colored_text(col::text_dimmer, "only used for the");
    colored_text(col::text_dimmer, "Shaders tab.");
    ImGui::Unindent(14);
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// ── Uniform widget ─────────────────────────────────────────────────────────
// Layout for a single parameter row, laid out by ImGui's normal cursor flow
// (NOT hand-positioned with SetCursorScreenPos):
//
//   ┌─ label (45%, clipped, left-aligned) ─┬─ control ─┬─ ↺ ─┐
//
// Why this is simpler than the previous 4-column hand-grid: ImGui owns the
// vertical centring (AlignTextToFramePadding) and the SameLine column
// boundary, so widgets that don't need full ctrl_w (Checkbox, ColorEdit
// preview swatch, ...) no longer collide with the value readout slot.
//
// Reference: source/runtime_gui.cpp::draw_variable_editor (ReShade upstream).
// Our row uses our custom slider visuals for float/int when a range is
// available; everything else is a stock ImGui widget so it can never
// overlap the label or the reset glyph.
void draw_uniform(mfx::runtime *rt, const UniformRow &u)
{
    using namespace theme;

    const uint32_t comps = (u.rows && u.rows <= 4) ? u.rows : 1;
    const bool is_float = (u.base_type == api::format::r32_float || u.base_type == api::format::r16_float);
    const bool is_bool  = (u.base_type == api::format::r32_typeless);
    const bool is_int   = (u.base_type == api::format::r32_sint   || u.base_type == api::format::r16_sint);
    const bool is_uint  = (u.base_type == api::format::r32_uint   || u.base_type == api::format::r16_uint);
    const bool has_items = (u.items[0] != '\0');
    const bool is_color  = (std::strcmp(u.ui_type, "color") == 0);
    const bool is_combo  = (std::strcmp(u.ui_type, "combo") == 0) && has_items;
    const bool is_list   = (std::strcmp(u.ui_type, "list")  == 0) && has_items;
    const bool is_radio  = (std::strcmp(u.ui_type, "radio") == 0) && has_items;
    const bool is_button = (std::strcmp(u.ui_type, "button") == 0) && is_bool;
    const bool is_drag   = (std::strcmp(u.ui_type, "drag") == 0);

    // Geometry. For sliders/drags: label on top, full-width control below.
    // For toggles/combos: label left, control right (single row).
    const float full_w  = ImGui::GetContentRegionAvail().x;
    const float reset_w = u.noreset ? 0.0f : 22.0f;
    const float gap     = 6.0f;

    // Determine if this control benefits from a two-row layout (slider/drag)
    const bool is_slider_type = !is_button && !is_bool && !is_color && !is_combo && !is_list && !is_radio;
    const float ctrl_w = is_slider_type
        ? (full_w - reset_w - gap)                   // full width for sliders
        : (full_w * 0.55f);                          // right-aligned for toggles/combos
    float label_w = is_slider_type ? full_w : (full_w - ctrl_w - reset_w - gap * 2.0f);
    if (label_w < 80.0f) label_w = 80.0f;

    if (u.noedit) ImGui::BeginDisabled();

    ImGui::Dummy(ImVec2(0, 4.0f)); // uniform vertical gap between parameters

    // ── Label row ────────────────────────────────────────────────────
    const float start_cursor_x = ImGui::GetCursorPosX(); // window-local X before label
    const ImVec2 row_top = ImGui::GetCursorScreenPos();
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, to_vec4(col::text_secondary));
    ImGui::TextUnformatted(u.label);
    ImGui::PopStyleColor();
    const bool label_hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);

    if (!is_slider_type) {
        // Single-row layout: control on the same line, right-aligned
        ImGui::SameLine();
        const float target_x = start_cursor_x + full_w - ctrl_w - reset_w - gap;
        ImGui::SetCursorPosX(target_x > ImGui::GetCursorPosX() ? target_x : ImGui::GetCursorPosX());
    }

    // ── Control column ───────────────────────────────────────────────
    bool changed = false;
    char id[160];
    snprintf(id, sizeof(id), "##v_%s", u.name);

    if (is_button) {
        bool v = false;
        rt->get_uniform_value_bool(u.handle, &v, 1, 0);
        if (ImGui::Button(u.label[0] ? u.label : "Apply", ImVec2(ctrl_w, 0))) {
            const bool t = true;
            rt->set_uniform_value_bool(u.handle, &t, 1, 0);
            changed = true;
        } else if (v) {
            const bool f = false;
            rt->set_uniform_value_bool(u.handle, &f, 1, 0);
        }
    }
    else if (is_bool) {
        bool v = false;
        rt->get_uniform_value_bool(u.handle, &v, 1, 0);
        if (toggle_switch(id, v, 40.0f, 22.0f)) {
            v = !v;
            rt->set_uniform_value_bool(u.handle, &v, 1, 0);
            changed = true;
        }
    }
    else if (is_color && is_float && (comps == 3 || comps == 4)) {
        float v[4] = { 0, 0, 0, 1 };
        rt->get_uniform_value_float(u.handle, v, (size_t)comps, 0);
        ImGuiColorEditFlags f = ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs;
        if (comps == 4) f |= ImGuiColorEditFlags_AlphaBar;
        ImGui::SetNextItemWidth(ctrl_w);
        if ((comps == 3 ? ImGui::ColorEdit3(id, v, f)
                        : ImGui::ColorEdit4(id, v, f))) {
            rt->set_uniform_value_float(u.handle, v, (size_t)comps, 0);
            changed = true;
        }
    }
    else if (is_radio) {
        // Radio buttons in a vertical group keep alignment predictable.
        int v = 0;
        if (is_int)        { int32_t  x = 0; rt->get_uniform_value_int (u.handle, &x, 1, 0); v = (int)x; }
        else if (is_uint)  { uint32_t x = 0; rt->get_uniform_value_uint(u.handle, &x, 1, 0); v = (int)x; }
        else if (is_float) { float    x = 0; rt->get_uniform_value_float(u.handle, &x, 1, 0); v = (int)x; }
        else if (is_bool)  { bool     x = 0; rt->get_uniform_value_bool (u.handle, &x, 1, 0); v = x ? 1 : 0; }

        ImGui::BeginGroup();
        int item_i = 0;
        for (const char *p = u.items; *p; p += std::strlen(p) + 1, ++item_i) {
            char rid[200];
            snprintf(rid, sizeof(rid), "%s##rad_%s_%d", p, u.name, item_i);
            if (ImGui::RadioButton(rid, v == item_i)) { v = item_i; changed = true; }
        }
        ImGui::EndGroup();
        if (changed) {
            if      (is_int)   { int32_t  x = (int32_t)v;  rt->set_uniform_value_int (u.handle, &x, 1, 0); }
            else if (is_uint)  { uint32_t x = (uint32_t)v; rt->set_uniform_value_uint(u.handle, &x, 1, 0); }
            else if (is_float) { float    x = (float)v;    rt->set_uniform_value_float(u.handle, &x, 1, 0); }
            else if (is_bool)  { bool     x = (v != 0);    rt->set_uniform_value_bool (u.handle, &x, 1, 0); }
        }
    }
    else if (is_combo || is_list) {
        int v = 0;
        if (is_int)        { int32_t  x = 0; rt->get_uniform_value_int (u.handle, &x, 1, 0); v = (int)x; }
        else if (is_uint)  { uint32_t x = 0; rt->get_uniform_value_uint(u.handle, &x, 1, 0); v = (int)x; }
        else if (is_float) { float    x = 0; rt->get_uniform_value_float(u.handle, &x, 1, 0); v = (int)x; }
        else if (is_bool)  { bool     x = 0; rt->get_uniform_value_bool (u.handle, &x, 1, 0); v = x ? 1 : 0; }

        ImGui::SetNextItemWidth(ctrl_w);
        if (is_list) {
            // Count items so the listbox height fits up to 6 entries.
            int n_items = 0;
            for (const char *p = u.items; *p; p += std::strlen(p) + 1) ++n_items;
            const int rows_show = n_items < 4 ? n_items : (n_items > 6 ? 6 : n_items);
            const float lb_h = ImGui::GetTextLineHeightWithSpacing() * (float)rows_show + 4.0f;
            if (ImGui::BeginListBox(id, ImVec2(ctrl_w, lb_h))) {
                int item_i = 0;
                for (const char *p = u.items; *p; p += std::strlen(p) + 1, ++item_i) {
                    if (ImGui::Selectable(p, v == item_i)) { v = item_i; changed = true; }
                }
                ImGui::EndListBox();
            }
        }
        else {
            if (ImGui::Combo(id, &v, u.items)) changed = true;
        }
        if (changed) {
            if      (is_int)   { int32_t  x = (int32_t)v;  rt->set_uniform_value_int (u.handle, &x, 1, 0); }
            else if (is_uint)  { uint32_t x = (uint32_t)v; rt->set_uniform_value_uint(u.handle, &x, 1, 0); }
            else if (is_float) { float    x = (float)v;    rt->set_uniform_value_float(u.handle, &x, 1, 0); }
            else if (is_bool)  { bool     x = (v != 0);    rt->set_uniform_value_bool (u.handle, &x, 1, 0); }
        }
    }
    else if (is_float) {
        float v[4] = { 0, 0, 0, 0 };
        rt->get_uniform_value_float(u.handle, v, (size_t)comps, 0);

        ImGui::Dummy(ImVec2(0, 2.0f)); // spacing between label and slider

        if (is_drag && !u.has_range) {
            const float step = (u.ui_step[0] > 0.0f) ? u.ui_step[0] : 0.01f;
            char fmt[24]; snprintf(fmt, sizeof(fmt), "%%.3f%s", u.ui_units[0] ? u.ui_units : "");
            ImGui::SetNextItemWidth(ctrl_w);
            if (ImGui::DragScalarN(id, ImGuiDataType_Float, v, (int)comps, step,
                                    nullptr, nullptr, fmt)) {
                rt->set_uniform_value_float(u.handle, v, (size_t)comps, 0);
                changed = true;
            }
        } else {
            const float vmin = u.has_range ? u.ui_min[0] : 0.0f;
            const float vmax = u.has_range ? u.ui_max[0] : 1.0f;
            const float step = (u.ui_step[0] > 0.0f) ? u.ui_step[0] : 0.0f;
            const float slider_h = 26.0f;
            const SliderResult sr = (comps == 1)
                ? mfx_slider_float_ex(id,        ctrl_w, slider_h, vmin, vmax, step, v, true)
                : mfx_slider_floatN_ex(id, comps, ctrl_w, slider_h, vmin, vmax, step, v);
            if (sr == SLIDER_CHANGED) {
                rt->set_uniform_value_float(u.handle, v, (size_t)comps, 0);
                changed = true;
            } else if (sr == SLIDER_RESET) {
                rt->reset_uniform_value(u.handle);
                changed = true;
            }
        }
    }
    else if (is_int || is_uint) {
        int32_t v[4] = { 0, 0, 0, 0 };
        if (is_int) rt->get_uniform_value_int(u.handle, v, (size_t)comps, 0);
        else        rt->get_uniform_value_uint(u.handle, reinterpret_cast<uint32_t *>(v), (size_t)comps, 0);

        ImGui::Dummy(ImVec2(0, 2.0f)); // spacing between label and slider

        if (is_drag && !u.has_range) {
            ImGui::SetNextItemWidth(ctrl_w);
            char fmt[24]; snprintf(fmt, sizeof(fmt), "%%d%s", u.ui_units[0] ? u.ui_units : "");
            if (ImGui::DragScalarN(id, is_int ? ImGuiDataType_S32 : ImGuiDataType_U32,
                                    v, (int)comps, 1.0f, nullptr, nullptr, fmt)) {
                if (is_int) rt->set_uniform_value_int(u.handle, v, (size_t)comps, 0);
                else        rt->set_uniform_value_uint(u.handle, reinterpret_cast<uint32_t *>(v), (size_t)comps, 0);
                changed = true;
            }
        } else {
            const int vmin = u.has_range ? (int)u.ui_min[0] : 0;
            const int vmax = u.has_range ? (int)u.ui_max[0] : 100;
            const float slider_h = 26.0f;
            SliderResult sr;
            if (comps == 1) {
                int iv = (int)v[0];
                sr = mfx_slider_int_ex(id, ctrl_w, slider_h, vmin, vmax, &iv);
                if (sr == SLIDER_CHANGED) v[0] = (int32_t)iv;
            } else {
                int iv[4] = { (int)v[0], (int)v[1], (int)v[2], (int)v[3] };
                sr = mfx_slider_intN_ex(id, comps, ctrl_w, slider_h, vmin, vmax, iv);
                if (sr == SLIDER_CHANGED)
                    for (int i = 0; i < (int)comps; ++i) v[i] = (int32_t)iv[i];
            }
            if (sr == SLIDER_CHANGED) {
                if (is_int) rt->set_uniform_value_int(u.handle, v, (size_t)comps, 0);
                else        rt->set_uniform_value_uint(u.handle, reinterpret_cast<uint32_t *>(v), (size_t)comps, 0);
                changed = true;
            } else if (sr == SLIDER_RESET) {
                rt->reset_uniform_value(u.handle);
                changed = true;
            }
        }
    }
    else {
        ImGui::TextDisabled("(unsupported type)");
    }

    // ── Reset glyph (right edge) ─────────────────────────────────────
    if (!u.noreset) {
        ImGui::SameLine();
        // Pin the reset glyph to the row's right edge regardless of how
        // wide the control turned out to be.
        const float dx = (row_top.x + full_w - 18.0f) - ImGui::GetCursorScreenPos().x;
        if (dx > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dx);
        char rid[160]; snprintf(rid, sizeof(rid), "##rst_%s", u.name);
        ImGui::PushStyleColor(ImGuiCol_Button,        IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col::bg_card_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  col::accent_subtle);
        if (ImGui::Button(rid, ImVec2(16.0f, 16.0f))) {
            rt->reset_uniform_value(u.handle);
            changed = true;
        }
        ImGui::PopStyleColor(3);
        const ImVec2 rc(ImGui::GetItemRectMin().x + 8.0f, ImGui::GetItemRectMin().y + 8.0f);
        const bool rhov = ImGui::IsItemHovered();
        if (rhov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Reset to default");
        }
        const ImU32 gc = rhov ? col::accent : IM_COL32(0xCC, 0xCC, 0xCC, 0x70);
        draw_reset_glyph(rc, 5.5f, gc);
    }

    // ── Tooltip on label hover ───────────────────────────────────────
    // ImGui's BeginTooltip ignores disabled state when AllowWhenDisabled
    // is set on the hover query above.
    if (u.tooltip[0] && label_hov) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        if (ImGui::BeginTooltip()) {
            ImGui::PushTextWrapPos(360.0f);
            ImGui::TextUnformatted(u.tooltip);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::PopStyleVar();
    }

    if (u.noedit) ImGui::EndDisabled();

    if (changed) {
        g_last_change_time = ImGui::GetTime();
        rt->save_current_preset();
    }
}

bool action_button(const char *label, ImU32 bg, ImU32 border, ImU32 fg, float h = 28.0f)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float w = ts.x + 22.0f;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b = ImVec2(a.x + w, a.y + h);

    ImGui::SetCursorScreenPos(a);
    ImGui::InvisibleButton(label, ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();

    dl->AddRectFilled(a, b, hov ? col::bg_card_hover : bg, size::radius_button);
    dl->AddRect      (a, b, border, size::radius_button);
    dl->AddText(ImVec2(a.x + 11.0f, a.y + (h - ts.y) * 0.5f), fg, label);

    return ImGui::IsItemClicked();
}

// Forward declarations for glyphs defined in the new pipeline UI section
// below  used here by the preset popup.
inline void glyph_search(ImDrawList *dl, ImVec2 c, ImU32 color);
