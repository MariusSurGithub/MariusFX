/* exports.hpp ──────────────────────────────────────────────────────────────
 *
 *   ABI between MariusFXUI.dll and its host process.
 *
 *   The host loads MariusFXUI.dll via LoadLibrary and resolves the entry
 *   points listed below. Both modules statically link the same ImGui
 *   binary (same source files, same compile flags), so the shared
 *   ImGuiContext* hands off cleanly between them. The host also provides
 *   its allocator functions so heap usage is consistent.
 *
 *   This header is included from BOTH sides — by exports.cpp inside the
 *   DLL and by the loader on the host side. The function signatures
 *   here therefore form the stable API surface; bumping it requires
 *   bumping MFXUI_ABI_VERSION (the host refuses to load a DLL that
 *   reports a different version).
 *
 *   All entry points are extern "C" / __cdecl — no name mangling, no
 *   unwinding across the boundary.
 *   ──────────────────────────────────────────────────────────────────── */

#pragma once

#include <imgui.h>

// Forward-declare the runtime pointer so this header doesn't drag in the
// whole effect-runtime API. Declared as struct to match its definition
// in the underlying api header.
namespace reshade { namespace api { struct effect_runtime; } }

// Bump on any breaking change to the function signatures below.
//   v1 : initial
//   v2 : adds MfxuiHostAPI + mfxui_set_host_api. Note: the struct has
//        since been trimmed to the four runtime-extension entries the
//        DLL actually uses — but the four kept entries sit at the
//        prefix of the original layout, so old hosts that wrote ten
//        entries are still ABI-compatible (DLL just ignores the rest).
constexpr unsigned int MFXUI_ABI_VERSION = 2;

#ifdef MFXUI_BUILDING_DLL
    #define MFXUI_API extern "C" __declspec(dllexport)
#else
    #define MFXUI_API extern "C" __declspec(dllimport)
#endif

// Exported names (resolved via GetProcAddress on the host side).
#define MFXUI_FN_ABI_VERSION       "mfxui_abi_version"
#define MFXUI_FN_INIT              "mfxui_init"
#define MFXUI_FN_SHUTDOWN          "mfxui_shutdown"
#define MFXUI_FN_RENDER            "mfxui_render"
#define MFXUI_FN_CONFIGURE_WINDOW  "mfxui_configure_next_window"
#define MFXUI_FN_STATE_GET         "mfxui_state_get"
#define MFXUI_FN_STATE_SET         "mfxui_state_set"
#define MFXUI_FN_SET_HOST_API      "mfxui_set_host_api"

// ── Host API bridge ────────────────────────────────────────────────────────
//
// Slim function-pointer table the host fills in and passes to the DLL via
// mfxui_set_host_api. Every entry covers a host-side capability that the
// DLL can't reach through the public effect_runtime API alone — typically
// non-virtual methods on the host's runtime class.
//
// Every pointer is allowed to be null; the DLL no-ops when a capability
// is missing instead of crashing.
struct MfxuiHostAPI
{
    void (*get_technique_timing)(reshade::api::effect_runtime *rt,
                                 unsigned long long tech_handle,
                                 unsigned long long *cpu_ns,
                                 unsigned long long *gpu_ns);
    bool (*get_performance_mode)(reshade::api::effect_runtime *rt);
    void (*set_performance_mode)(reshade::api::effect_runtime *rt, bool v);
    void (*reload_all)          (reshade::api::effect_runtime *rt);
};

// ── Entry points ────────────────────────────────────────────────────────────

// Returns MFXUI_ABI_VERSION. Always called first by the host so it can
// refuse to load a mismatched DLL before invoking anything else.
MFXUI_API unsigned int mfxui_abi_version();

// Called once right after LoadLibrary. Adopts the host's ImGui context
// and allocators, then runs MariusFX UI's one-time setup (theme, etc.).
MFXUI_API void mfxui_init(
    ImGuiContext       *ctx,
    ImGuiMemAllocFunc   alloc_fn,
    ImGuiMemFreeFunc    free_fn,
    void               *user_data);

// Called once before FreeLibrary. The DLL must release every host-owned
// handle and detach from the ImGui context — after this call returns the
// host is free to unload the module.
MFXUI_API void mfxui_shutdown();

// Per-frame UI render. Called inside the host's ImGui::Begin/End for the
// MariusFX overlay.
MFXUI_API void mfxui_render(reshade::api::effect_runtime *rt);

// Called BEFORE the host's ImGui::Begin. Returns the window flags the
// host should pass to Begin and pre-configures next-window pos/size for
// the snapped overlay layout.
MFXUI_API ImGuiWindowFlags mfxui_configure_next_window(
    ImVec2 viewport_pos, ImVec2 viewport_size);

// State persistence across hot-reloads.
//
//   *out_buf       : caller-owned buffer (or null to query required size).
//    out_buf_size  : size of *out_buf.
//   returns        : number of bytes written (or required if buf was null).
//
// The host calls mfxui_state_get with buf=null first to discover the size,
// allocates a buffer, calls again to fill it, then on the next instance
// after reload calls mfxui_state_set to restore. The blob is opaque — the
// DLL is free to version-tag it internally and refuse to deserialize old
// versions.
MFXUI_API size_t mfxui_state_get(void *out_buf, size_t out_buf_size);
MFXUI_API void   mfxui_state_set(const void *buf, size_t size);

// Optional bridge — installs the host-provided function pointers used by
// the DLL to access non-virtual runtime extensions. Must be called once,
// AFTER mfxui_init. Passing null clears any previously-installed table.
// The struct is borrowed, not copied — the host must keep it alive until
// the next call (or until mfxui_shutdown).
MFXUI_API void mfxui_set_host_api(const MfxuiHostAPI *api);

// ── Function-pointer typedefs (used by the host loader) ─────────────────────
typedef unsigned int     (*mfxui_abi_version_fn)();
typedef void             (*mfxui_init_fn)(ImGuiContext*, ImGuiMemAllocFunc, ImGuiMemFreeFunc, void*);
typedef void             (*mfxui_shutdown_fn)();
typedef void             (*mfxui_render_fn)(reshade::api::effect_runtime*);
typedef ImGuiWindowFlags (*mfxui_configure_next_window_fn)(ImVec2, ImVec2);
typedef size_t           (*mfxui_state_get_fn)(void*, size_t);
typedef void             (*mfxui_state_set_fn)(const void*, size_t);
typedef void             (*mfxui_set_host_api_fn)(const MfxuiHostAPI*);
