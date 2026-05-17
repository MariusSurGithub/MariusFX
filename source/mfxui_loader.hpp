/*
 * mfxui_loader — host-side dynamic loader for MariusFXUI.dll.
 *
 *   The MariusFX UI lives in a separate DLL (MariusFXUI.dll) so it can be
 *   rebuilt and hot-swapped while FiveM keeps running. This module:
 *
 *     • LoadLibrary's MariusFXUI.dll from the directory hosting the
 *       ReShade DLL, into a per-load copy so the source file stays free
 *       for the build script to overwrite.
 *
 *     • Watches the source DLL's mtime; when it changes, the previous
 *       module is unloaded and a fresh copy is loaded — all UI state
 *       that the DLL chose to expose via mfxui_state_get is preserved
 *       across the swap.
 *
 *     • Provides thin forwarders the host calls instead of the direct
 *       mariusfx::ui::{init,render,configure_next_window} functions
 *       (those are now compiled inside the DLL, not the host).
 *
 *   The two heavy invariants:
 *
 *     1. ImGui context sharing — the DLL inherits the host's
 *        ImGuiContext* + allocators via mfxui_init so both modules see
 *        the same context object and use the same heap.
 *
 *     2. ABI versioning — the DLL exports mfxui_abi_version(); a
 *        mismatch with MFXUI_ABI_VERSION refuses the load (no half-
 *        compatible state in the GUI).
 */

#pragma once

#include <imgui.h>

namespace reshade { namespace api { struct effect_runtime; } }

namespace mariusfx::loader {

// Called once per frame, BEFORE configure_next_window / render. Loads the
// DLL on first call, watches its mtime, and reloads in place when the
// source DLL changes on disk.
void tick();

// Returns true if the DLL is currently loaded and the function pointers
// are valid. The host should still call render() either way — when it
// returns false render() is a no-op so the rest of the GUI keeps working.
bool is_loaded();

// Forwarders. Each is a no-op when is_loaded() is false (DLL missing,
// failed to load, or in the middle of a reload).
ImGuiWindowFlags configure_next_window(ImVec2 viewport_pos, ImVec2 viewport_size);
void             render(reshade::api::effect_runtime *rt);

// Manual reload trigger (e.g. bound to a hotkey).
void request_reload();

// Diagnostics for the host status bar / debug overlay.
struct Status {
    bool         loaded;
    int          load_index;          // increments on each successful (re)load
    long long    last_load_time_unix; // seconds since epoch
    char         last_error[256];     // empty if no error
};
Status status();

} // namespace mariusfx::loader
