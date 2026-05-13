# mariusfx/

All MariusFX-specific code lives here, kept separate from the
post-processing engine sources in `source/` so engine updates can
be merged in cleanly.

## Subdirectories

| Path | Purpose |
|------|---------|
| `ui_safe_mask/` | Backbuffer-diff capture and final UI-mask compositing pass. Hooks `Draw*` and `OMSetRenderTargets` via the same MinHook infrastructure the engine already uses. Two textures: `scene_clean` (snapshot taken after the first BB-targeted draw of the frame, i.e. the post-FX composite blit) and `bb_with_ui` (snapshot taken at the start of `runtime::on_present`, before any effect runs). The compositing PS computes `ui_mask = smoothstep(0.005, 0.05, length(bb_with_ui - scene_clean))` and lerps the live BB back to `bb_with_ui` where `ui_mask > 0`. |
| `ui/`           | The in-game configuration overlay. Theme: dark-blue dashboard, palette `#080A0F` / `#06080C`, accent `#4F7EF8`, fonts DM Sans + DM Mono + Lucide icons. Layout: titlebar (tabs + live perf stats), sidebar (preset / search / filter pills / shader list), detail panel (header + parameters / live preview / render-order list), bottombar (reload / performance mode / shortcuts). |
| `fivem_glue/`   | FiveM-specific glue: `GetRawInputData` hook that zeroes mouse deltas while the overlay is visible (so the GTA camera doesn't drift), vtable-healing for the swapchain/context interfaces in case the host application repatches them. |

## Engine integration points

Only **three** engine source files carry MariusFX additions; all
other files in `source/` are left bit-identical to make engine
updates merge-friendly:

1. `source/runtime.cpp` — two one-line calls in `on_present()`:
   ```cpp
   mariusfx::ui_mask::capture_bb_with_ui(cmd_list, back_buffer_resource);
   runtime::render_effects(...);
   mariusfx::ui_mask::apply(cmd_list, _back_buffer_targets[...]);
   ```
2. `source/d3d11/d3d11_command_list.cpp` — `Draw*` and
   `OMSetRenderTargets` thunks call into `mariusfx::ui_mask::on_*`
   to drive the scene-clean snapshot.
3. `source/runtime_gui.cpp` — `draw_gui()` body delegates to
   `mariusfx::ui::render()`. The ImGui infrastructure
   (font atlas, render data submission, resource init/destroy)
   stays untouched.
