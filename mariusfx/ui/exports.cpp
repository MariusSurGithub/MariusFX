/* exports.cpp ──────────────────────────────────────────────────────────────
 *
 *   Hot-reloadable DLL entry points for MariusFXUI.
 *
 *   The DLL holds its own static copy of ImGui (compiled from the same
 *   sources as the host). Sharing the host's ImGuiContext* + allocators
 *   on init() makes the two ImGui copies behave as a single instance —
 *   ImGui's per-binary statics still function, but they all point at the
 *   same context object and use the same heap.
 *
 *   See exports.hpp for the ABI contract.
 *   ──────────────────────────────────────────────────────────────────── */

// windows.h MUST come first so GUID-related macros are defined before any
// header that uses __declspec(uuid(...)) (e.g. reshade_api.hpp via ui.hpp).
#include <windows.h>

#define MFXUI_BUILDING_DLL 1
#include "exports.hpp"
#include "ui.hpp"
#include <cstring>

// ── ABI handshake ───────────────────────────────────────────────────────────
MFXUI_API unsigned int mfxui_abi_version()
{
    return MFXUI_ABI_VERSION;
}

// ── Init / shutdown ─────────────────────────────────────────────────────────
//
// Adopt the host's ImGui context + allocators so every ImGui::Foo() call
// inside this DLL operates on the same context object as the host. This
// is the standard "ImGui in a plugin" pattern documented at:
//   https://github.com/ocornut/imgui/blob/master/docs/FAQ.md
//   ("How can I use ImGui from multiple modules")
//
MFXUI_API void mfxui_init(
    ImGuiContext       *ctx,
    ImGuiMemAllocFunc   alloc_fn,
    ImGuiMemFreeFunc    free_fn,
    void               *user_data)
{
    ImGui::SetCurrentContext(ctx);
    if (alloc_fn != nullptr && free_fn != nullptr)
        ImGui::SetAllocatorFunctions(alloc_fn, free_fn, user_data);

    mariusfx::ui::init();
}

MFXUI_API void mfxui_shutdown()
{
    // Detach from the host context so a stale pointer can't be used after
    // FreeLibrary. The host re-publishes the context on every reload.
    ImGui::SetCurrentContext(nullptr);
}

// ── Per-frame ───────────────────────────────────────────────────────────────
MFXUI_API void mfxui_render(reshade::api::effect_runtime *rt)
{
    // In the DLL build reshade::runtime is aliased to api::effect_runtime
    // (see ui.hpp), so this is a direct call — no downcast required.
    mariusfx::ui::render(rt);
}

MFXUI_API ImGuiWindowFlags mfxui_configure_next_window(
    ImVec2 viewport_pos, ImVec2 viewport_size)
{
    return mariusfx::ui::configure_next_window(viewport_pos, viewport_size);
}

// ── State persistence (Phase 4) ─────────────────────────────────────────────
//
// We expose a tiny POD blob holding the user-visible UI settings that
// would otherwise be lost on reload (dock side, panel width, current
// selection, search filter, active sheet). The blob is versioned so a
// future schema change can be skipped silently rather than corrupting
// memory.
//
// Definitions of the underlying globals live in ui.cpp; they are exposed
// here through small accessor helpers in mariusfx::ui (see ui.hpp).

namespace mariusfx::ui {
// Implemented in ui.cpp.
size_t persistent_state_size();
void   persistent_state_save(void *buf, size_t buf_size);
void   persistent_state_load(const void *buf, size_t size);
} // namespace mariusfx::ui

MFXUI_API size_t mfxui_state_get(void *out_buf, size_t out_buf_size)
{
    const size_t need = mariusfx::ui::persistent_state_size();
    if (out_buf == nullptr || out_buf_size < need)
        return need;
    mariusfx::ui::persistent_state_save(out_buf, out_buf_size);
    return need;
}

MFXUI_API void mfxui_state_set(const void *buf, size_t size)
{
    if (buf == nullptr || size == 0) return;
    mariusfx::ui::persistent_state_load(buf, size);
}

// ── Host API bridge ─────────────────────────────────────────────────────────
//
// The DLL stores the host-provided pointer table in a single global, which
// the rt_* wrappers + the new host_* wrappers (declared in ui.cpp) inspect
// at call time. Borrowing semantics: we keep the pointer the host gives us
// and trust them not to free it before shutdown.
namespace mariusfx::ui {
const MfxuiHostAPI *g_host_api = nullptr;
}

MFXUI_API void mfxui_set_host_api(const MfxuiHostAPI *api)
{
    mariusfx::ui::g_host_api = api;
}

// ── DllMain ─────────────────────────────────────────────────────────────────
// Bare-bones — we don't need TLS callbacks; everything runs through the
// explicit init/shutdown pair.
#include <windows.h>
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(GetModuleHandleA(nullptr));
    return TRUE;
}
