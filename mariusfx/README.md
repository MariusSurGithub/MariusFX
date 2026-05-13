# mariusfx/

All MariusHybridFX-Reshade additions live here so the upstream ReShade
tree stays touchable for `git merge upstream/main`.

## Planned subdirectories

| Path | Purpose |
|------|---------|
| `ui_safe_mask/` | BB-diff capture + final UI-mask compositing pass. Hooks Draw\*/OMSetRenderTargets via the same MinHook infrastructure ReShade already uses. Two textures: `scene_clean` (snapshot taken after the first BB-targeted draw of the frame, i.e. the post-FX composite) and `bb_with_ui` (snapshot taken at the start of `runtime::on_present`, before ReShade applies any effect). The PS computes `ui_mask = smoothstep(0.005, 0.05, length(bb_with_ui - scene_clean))` and lerps the live BB back to `bb_with_ui` where `ui_mask > 0`. |
| `ui/`           | Custom ImGui overlay replacing `runtime_gui.cpp::draw_gui*`. Theme matches the dark-blue dashboard mockup (palette `#080A0F` / `#06080C`, accent `#4F7EF8`, fonts DM Sans + DM Mono + Lucide icons). Layout: titlebar (tabs + perf stats), sidebar (preset/search/filter pills/shader list), detail panel (header + params/preview/render-order), bottombar. |
| `fivem_glue/`   | FiveM-specific tweaks ported from the original MariusHybridFX POC: `GetRawInputData` hook to freeze the GTA camera while the overlay is open, vtable-healing for swapchain/context. |

## Integration points in upstream code

Only **3 files** are modified upstream:

1. `source/runtime.cpp` — two 1-line calls in `on_present()`:
   ```cpp
   mariusfx::ui_mask::capture_bb_with_ui(cmd_list, back_buffer_resource);
   runtime::render_effects(...);
   mariusfx::ui_mask::apply(cmd_list, _back_buffer_targets[...]);
   ```
2. `source/d3d11/d3d11_command_list.cpp` — Draw\* and OMSetRenderTargets
   thunks call into `mariusfx::ui_mask::on_*` to drive the snapshot.
3. `source/runtime_gui.cpp` — `draw_gui()` body delegates to `mariusfx::ui::render()`.

Every other file stays bit-identical to upstream so future merges are
trivial.
