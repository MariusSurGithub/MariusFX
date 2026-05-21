/*
 * mfxui_loader.cpp — implementation. See mfxui_loader.hpp for the rationale.
 */

#include "mfxui_loader.hpp"
#include "../mariusfx/ui/exports.hpp"
#include "runtime.hpp"

#include <windows.h>
#include <imgui.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace mariusfx::loader {

namespace {

// ── Loader state ────────────────────────────────────────────────────────────
struct LoaderState {
    HMODULE module = nullptr;

    // Resolved entry points. Null when no DLL is loaded.
    mfxui_abi_version_fn           abi_version           = nullptr;
    mfxui_init_fn                  init                  = nullptr;
    mfxui_shutdown_fn              shutdown              = nullptr;
    mfxui_render_fn                render                = nullptr;
    mfxui_configure_next_window_fn configure_next_window = nullptr;
    mfxui_state_get_fn             state_get             = nullptr;
    mfxui_state_set_fn             state_set             = nullptr;
    mfxui_set_host_api_fn          set_host_api          = nullptr;

    fs::path                       source_path;     // canonical MariusFXUI.dll
    fs::path                       loaded_path;     // the rolling _loaded_N.dll copy
    fs::file_time_type             source_mtime{};
    int                            load_index   = 0;

    // Survives reloads: last good DLL state blob, captured on shutdown,
    // re-injected after init.
    std::vector<unsigned char>     persistent_state;

    bool                           reload_requested = false;
    fs::file_time_type             pending_mtime{};
    std::chrono::steady_clock::time_point pending_mtime_first_seen{};

    long long                      last_load_time_unix = 0;
    char                           last_error[256]     = "";
};

LoaderState g_l;

// ── Helpers ─────────────────────────────────────────────────────────────────
void set_error(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(g_l.last_error, sizeof(g_l.last_error), fmt, ap);
    va_end(ap);
}

fs::path locate_source_dll()
{
    // Resolve the directory hosting THIS code. In production the host DLL
    // is renamed to dxgi.dll inside FiveM's plugins folder, so probing for
    // "ReShade64.dll" by name doesn't work. GetModuleHandleEx with an
    // address inside this translation unit returns the right HMODULE
    // regardless of how the host was named.
    HMODULE me = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&locate_source_dll),
        &me);
    char buf[MAX_PATH] = "";
    GetModuleFileNameA(me, buf, MAX_PATH);
    return fs::path(buf).parent_path() / "MariusFXUI.dll";
}

// Try to remove every previously-loaded copy that's no longer in use.
void cleanup_stale_copies()
{
    if (g_l.source_path.empty()) return;
    std::error_code ec;
    for (auto &de : fs::directory_iterator(g_l.source_path.parent_path(), ec))
    {
        if (!de.is_regular_file(ec)) continue;
        const std::string name = de.path().filename().string();
        if (name.rfind("MariusFXUI_loaded_", 0) != 0) continue;
        if (g_l.loaded_path == de.path()) continue;
        std::error_code rm_ec;
        fs::remove(de.path(), rm_ec); // best-effort, ignore failures
    }
}

void unload_current()
{
    if (g_l.module == nullptr) return;

    // Snapshot UI state before tearing the DLL down.
    if (g_l.state_get != nullptr) {
        const size_t need = g_l.state_get(nullptr, 0);
        g_l.persistent_state.resize(need);
        if (need > 0) g_l.state_get(g_l.persistent_state.data(), need);
    }

    if (g_l.shutdown != nullptr) g_l.shutdown();

    FreeLibrary(g_l.module);
    g_l.module                = nullptr;
    g_l.abi_version           = nullptr;
    g_l.init                  = nullptr;
    g_l.shutdown              = nullptr;
    g_l.render                = nullptr;
    g_l.configure_next_window = nullptr;
    g_l.state_get             = nullptr;
    g_l.state_set             = nullptr;
    g_l.set_host_api          = nullptr;
}

bool resolve_exports()
{
    auto get = [&](const char *name) -> FARPROC {
        FARPROC p = GetProcAddress(g_l.module, name);
        if (p == nullptr) set_error("GetProcAddress(%s) failed", name);
        return p;
    };
    g_l.abi_version           = (mfxui_abi_version_fn)          get(MFXUI_FN_ABI_VERSION);
    g_l.init                  = (mfxui_init_fn)                 get(MFXUI_FN_INIT);
    g_l.shutdown              = (mfxui_shutdown_fn)             get(MFXUI_FN_SHUTDOWN);
    g_l.render                = (mfxui_render_fn)               get(MFXUI_FN_RENDER);
    g_l.configure_next_window = (mfxui_configure_next_window_fn)get(MFXUI_FN_CONFIGURE_WINDOW);
    g_l.state_get             = (mfxui_state_get_fn)            get(MFXUI_FN_STATE_GET);
    g_l.state_set             = (mfxui_state_set_fn)            get(MFXUI_FN_STATE_SET);
    // set_host_api is optional — v1 DLLs don't export it. Clear the error
    // it sets via GetProcAddress if it's missing; we'll just skip the
    // host-api install step in that case.
    g_l.set_host_api          = (mfxui_set_host_api_fn)         GetProcAddress(g_l.module, MFXUI_FN_SET_HOST_API);
    return  g_l.abi_version           != nullptr &&
            g_l.init                  != nullptr &&
            g_l.shutdown              != nullptr &&
            g_l.render                != nullptr &&
            g_l.configure_next_window != nullptr;
}

bool load_fresh()
{
    if (g_l.source_path.empty())
        g_l.source_path = locate_source_dll();
    std::error_code ec;
    if (!fs::exists(g_l.source_path, ec)) {
        set_error("MariusFXUI.dll not found at %s", g_l.source_path.string().c_str());
        return false;
    }

    // Copy the source DLL to a per-load filename so the build script can
    // freely overwrite the canonical location while the previous instance
    // is still running.
    const fs::path dir = g_l.source_path.parent_path();
    char copy_name[64];
    std::snprintf(copy_name, sizeof(copy_name),
                  "MariusFXUI_loaded_%d.dll", ++g_l.load_index);
    const fs::path copy_path = dir / copy_name;
    fs::copy_file(g_l.source_path, copy_path,
                  fs::copy_options::overwrite_existing, ec);
    if (ec) {
        set_error("copy %s -> %s failed: %s",
                  g_l.source_path.string().c_str(),
                  copy_path.string().c_str(),
                  ec.message().c_str());
        return false;
    }

    g_l.loaded_path = copy_path;
    g_l.module = LoadLibraryW(copy_path.wstring().c_str());
    if (g_l.module == nullptr) {
        set_error("LoadLibrary(%s) failed: %lu",
                  copy_path.string().c_str(), GetLastError());
        return false;
    }

    if (!resolve_exports()) {
        FreeLibrary(g_l.module);
        g_l.module = nullptr;
        return false;
    }

    if (g_l.abi_version() != MFXUI_ABI_VERSION) {
        set_error("ABI version mismatch (DLL=%u, host=%u)",
                  g_l.abi_version(), MFXUI_ABI_VERSION);
        FreeLibrary(g_l.module);
        g_l.module = nullptr;
        return false;
    }

    // Hand the DLL the host's ImGui context + allocators.
    ImGuiContext *ctx = ImGui::GetCurrentContext();
    ImGuiMemAllocFunc alloc_fn = nullptr;
    ImGuiMemFreeFunc  free_fn  = nullptr;
    void             *user_data = nullptr;
    ImGui::GetAllocatorFunctions(&alloc_fn, &free_fn, &user_data);

    g_l.init(ctx, alloc_fn, free_fn, user_data);

    // Install the host API bridge — gives the DLL access to the four
    // non-virtual runtime methods it needs (mariusfx_*). The struct is a
    // file-scope static so the borrowed pointer stays valid for the
    // lifetime of the loaded DLL.
    if (g_l.set_host_api != nullptr)
    {
        static const MfxuiHostAPI s_host_api = {
            // ── v2 original ────────────────────────────────────────
            // get_technique_timing
            [](reshade::api::effect_runtime *rt,
               unsigned long long handle,
               unsigned long long *cpu_ns, unsigned long long *gpu_ns) {
                reshade::api::effect_technique t{};
                t.handle = handle;
                uint64_t c = 0, g = 0;
                static_cast<reshade::runtime*>(rt)->mariusfx_get_technique_timing(t, &c, &g);
                *cpu_ns = c; *gpu_ns = g;
            },
            // get_performance_mode
            [](reshade::api::effect_runtime *rt) -> bool {
                return static_cast<reshade::runtime*>(rt)->mariusfx_get_performance_mode();
            },
            // set_performance_mode
            [](reshade::api::effect_runtime *rt, bool v) {
                static_cast<reshade::runtime*>(rt)->mariusfx_set_performance_mode(v);
            },
            // reload_all
            [](reshade::api::effect_runtime *rt) {
                static_cast<reshade::runtime*>(rt)->mariusfx_reload_all();
            },

            // ── v2+ extensions: screenshot config ──────────────────
            // get_screenshot_path
            [](reshade::api::effect_runtime *rt, char *buf, unsigned int sz) {
                auto *r = static_cast<reshade::runtime*>(rt);
                std::string s = r->mariusfx_screenshot_path().u8string();
                snprintf(buf, sz, "%s", s.c_str());
            },
            // set_screenshot_path
            [](reshade::api::effect_runtime *rt, const char *path) {
                static_cast<reshade::runtime*>(rt)->mariusfx_screenshot_path() = std::filesystem::u8path(path);
            },
            // get_screenshot_name
            [](reshade::api::effect_runtime *rt, char *buf, unsigned int sz) {
                auto *r = static_cast<reshade::runtime*>(rt);
                snprintf(buf, sz, "%s", r->mariusfx_screenshot_name().c_str());
            },
            // set_screenshot_name
            [](reshade::api::effect_runtime *rt, const char *name) {
                static_cast<reshade::runtime*>(rt)->mariusfx_screenshot_name() = name;
            },
            // get_screenshot_format
            [](reshade::api::effect_runtime *rt) -> unsigned int {
                return static_cast<reshade::runtime*>(rt)->mariusfx_screenshot_format();
            },
            // set_screenshot_format
            [](reshade::api::effect_runtime *rt, unsigned int fmt) {
                static_cast<reshade::runtime*>(rt)->mariusfx_screenshot_format() = fmt;
            },
            // get_screenshot_quality
            [](reshade::api::effect_runtime *rt) -> unsigned int {
                return static_cast<reshade::runtime*>(rt)->mariusfx_screenshot_quality();
            },
            // set_screenshot_quality
            [](reshade::api::effect_runtime *rt, unsigned int q) {
                static_cast<reshade::runtime*>(rt)->mariusfx_screenshot_quality() = q;
            },

            // ── v2+ extensions: hotkey config ──────────────────────
            // get_overlay_key
            [](reshade::api::effect_runtime *rt, unsigned int out[4]) {
                memcpy(out, static_cast<reshade::runtime*>(rt)->mariusfx_overlay_key_data(), sizeof(unsigned int) * 4);
            },
            // set_overlay_key
            [](reshade::api::effect_runtime *rt, const unsigned int data[4]) {
                memcpy(static_cast<reshade::runtime*>(rt)->mariusfx_overlay_key_data(), data, sizeof(unsigned int) * 4);
            },
            // get_screenshot_key
            [](reshade::api::effect_runtime *rt, unsigned int out[4]) {
                memcpy(out, static_cast<reshade::runtime*>(rt)->mariusfx_screenshot_key_data(), sizeof(unsigned int) * 4);
            },
            // set_screenshot_key
            [](reshade::api::effect_runtime *rt, const unsigned int data[4]) {
                memcpy(static_cast<reshade::runtime*>(rt)->mariusfx_screenshot_key_data(), data, sizeof(unsigned int) * 4);
            },
            // get_effects_key
            [](reshade::api::effect_runtime *rt, unsigned int out[4]) {
                memcpy(out, static_cast<reshade::runtime*>(rt)->mariusfx_effects_key_data(), sizeof(unsigned int) * 4);
            },
            // set_effects_key
            [](reshade::api::effect_runtime *rt, const unsigned int data[4]) {
                memcpy(static_cast<reshade::runtime*>(rt)->mariusfx_effects_key_data(), data, sizeof(unsigned int) * 4);
            },

            // ── save_config ────────────────────────────────────────
            [](reshade::api::effect_runtime *rt) {
                static_cast<reshade::runtime*>(rt)->mariusfx_save_config();
            },
        };
        g_l.set_host_api(&s_host_api);
    }

    // Restore state from the previous instance (if any).
    if (!g_l.persistent_state.empty() && g_l.state_set != nullptr)
        g_l.state_set(g_l.persistent_state.data(),
                      g_l.persistent_state.size());

    g_l.source_mtime = fs::last_write_time(g_l.source_path, ec);
    g_l.last_load_time_unix = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    g_l.last_error[0] = '\0';

    cleanup_stale_copies();
    return true;
}

bool is_modified()
{
    if (g_l.source_path.empty()) return false;
    std::error_code ec;
    if (!fs::exists(g_l.source_path, ec)) return false;
    const auto cur = fs::last_write_time(g_l.source_path, ec);
    if (ec) return false;
    if (cur == g_l.source_mtime) return false;

    // Debounce: a build emits multiple writes (link, then sign-and-rename).
    // Wait until the mtime has been stable for ≥250 ms before triggering.
    if (cur != g_l.pending_mtime) {
        g_l.pending_mtime            = cur;
        g_l.pending_mtime_first_seen = std::chrono::steady_clock::now();
        return false;
    }
    const auto elapsed =
        std::chrono::steady_clock::now() - g_l.pending_mtime_first_seen;
    return elapsed >= std::chrono::milliseconds(250);
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────────────
void tick()
{
    if (g_l.module == nullptr) {
        load_fresh();           // first-time load (or retry after failure)
        return;
    }
    if (g_l.reload_requested || is_modified()) {
        g_l.reload_requested = false;
        unload_current();
        load_fresh();
    }
}

bool is_loaded()
{
    return g_l.module != nullptr && g_l.render != nullptr;
}

ImGuiWindowFlags configure_next_window(ImVec2 viewport_pos, ImVec2 viewport_size)
{
    if (g_l.configure_next_window == nullptr) {
        // Fallback: lock the window to the full viewport so the host's
        // Begin() doesn't go to (0,0)+(default size) in the rare instant
        // between failed-load attempts.
        ImGui::SetNextWindowPos (viewport_pos,  ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport_size, ImGuiCond_Always);
        return ImGuiWindowFlags_NoDecoration |
               ImGuiWindowFlags_NoNav        |
               ImGuiWindowFlags_NoMove       |
               ImGuiWindowFlags_NoResize     |
               ImGuiWindowFlags_NoSavedSettings |
               ImGuiWindowFlags_NoBringToFrontOnFocus;
    }
    return g_l.configure_next_window(viewport_pos, viewport_size);
}

void render(reshade::api::effect_runtime *rt)
{
    if (g_l.render == nullptr) {
        // Show a tiny diagnostic panel so the user can see why the UI
        // didn't appear (DLL missing, ABI mismatch, …).
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
        ImGui::Text("MariusFX UI failed to load.");
        ImGui::PopStyleColor();
        if (g_l.last_error[0])
            ImGui::TextWrapped("%s", g_l.last_error);
        else
            ImGui::TextDisabled("(MariusFXUI.dll not found next to ReShade64.dll)");
        return;
    }
    g_l.render(rt);
}

void request_reload()
{
    g_l.reload_requested = true;
}

Status status()
{
    Status s{};
    s.loaded              = is_loaded();
    s.load_index          = g_l.load_index;
    s.last_load_time_unix = g_l.last_load_time_unix;
    std::strncpy(s.last_error, g_l.last_error, sizeof(s.last_error));
    s.last_error[sizeof(s.last_error) - 1] = '\0';
    return s;
}

} // namespace mariusfx::loader
