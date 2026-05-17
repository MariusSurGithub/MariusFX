/*
 * MariusFX — custom in-game overlay entry point.
 *
 * The runtime calls `render(runtime*)` once per frame from inside its
 * existing draw_gui() function (we patch the upstream draw_gui to
 * delegate to this). The runtime keeps owning ImGui init/shutdown,
 * font atlas building, and ImDrawData submission to D3D — we only
 * own the visible widgets.
 */

#pragma once

#include <imgui.h>

#ifdef MARIUSFX_HOT_DLL
  // Hot-reload DLL build: ui.cpp aliases reshade::runtime to api::effect_runtime
  // (see ui.cpp). To avoid a clash between a class forward-decl here and a
  // using-alias in ui.cpp, we pull the public header instead and reuse the
  // same alias here.
  #include "../../include/reshade_api.hpp"
  namespace reshade { using runtime = api::effect_runtime; }
#else
  namespace reshade { class runtime; }
#endif

namespace mariusfx::ui {

// Compute and apply position / size / size-constraints for the next
// ImGui::Begin() call hosting the overlay. The overlay is docked to the
// left or right side of the viewport (user-togglable) with full height
// and user-resizable width. Returns the ImGuiWindowFlags the caller
// should pass to Begin().
//
// Call sequence in the host:
//     const ImGuiWindowFlags flags = mariusfx::ui::configure_next_window(viewport_pos, viewport_size);
//     ImGui::Begin("##mariusfx_overlay", nullptr, flags);
//     mariusfx::ui::render(rt);
//     ImGui::End();
ImGuiWindowFlags configure_next_window(ImVec2 viewport_pos, ImVec2 viewport_size);

// Render the MariusFX overlay for the current frame. Must be called
// between ImGui::NewFrame() and ImGui::Render(); the runtime guarantees
// this when invoked from draw_gui().
void render(reshade::runtime *rt);

// Called once when the runtime initialises ImGui — pushes our theme
// onto the global ImGuiStyle. Idempotent.
void init();

// ── Hot-reload state persistence ────────────────────────────────────────────
// Cross-reload survival of user-visible UI settings (dock side, panel
// width, current selection, search filter, active sheet). The blob is
// opaque to the host; the DLL versions it internally.
size_t persistent_state_size();
void   persistent_state_save(void *buf, size_t buf_size);
void   persistent_state_load(const void *buf, size_t size);

} // namespace mariusfx::ui
