/*
 * MariusFX — UI-safe post-process compositing pass (BB-diff masking).
 *
 * Two snapshots of the swapchain back buffer are kept per frame:
 *
 *   scene_clean : taken just after the FIRST draw that targeted the BB
 *                 in the current frame (= the post-FX composite blit
 *                 the host application emits before drawing its UI).
 *                 Contains the rendered scene with no UI on it.
 *
 *   bb_with_ui  : taken at the very start of `runtime::on_present`,
 *                 before any effect runs. Contains the rendered scene
 *                 PLUS the UI/HUD/NUI the host application drew on top.
 *
 * After `render_effects` has mangled the BB with the user-selected
 * effects, the compositing PS computes
 *
 *     ui_mask = smoothstep(0.005, 0.05, length(bb_with_ui - scene_clean))
 *     final   = lerp(effects_result, bb_with_ui, ui_mask)
 *
 * which restores UI pixels verbatim while keeping every effect on the
 * pure-scene pixels. There is no per-draw stencil tagging, so the
 * approach is shader-agnostic and works regardless of how the host
 * application renders its UI.
 *
 * The whole pass costs ~0.1ms at 1080p (one extra CopyResource for
 * `effects_result` plus a fullscreen-triangle PS sampling 3 textures).
 */

#pragma once

#include <d3d11.h>
#include <dxgi.h>

namespace mariusfx::ui_safe_mask {

// Called from D3D11DeviceContext::Draw* AFTER the original draw runs.
// Cheap fast path: one atomic load when the snapshot is already taken.
void on_draw(ID3D11DeviceContext *ctx);

// Called from D3D11DeviceContext::OMSetRenderTargets[AndUAVs] AFTER
// the original call runs. Updates a thread-local flag indicating
// whether RT slot 0 currently points at the swapchain BB resource.
void on_omset_rt(ID3D11DeviceContext *ctx,
                 UINT num_views,
                 ID3D11RenderTargetView *const *rtvs);

// Called from runtime::on_present at the very start, before any
// effect runs. Captures `bb_with_ui` so the compositing pass can
// later restore UI pixels even after effects mangle the BB.
void on_swapchain_present_begin(ID3D11DeviceContext *ctx,
                                IDXGISwapChain *sc,
                                ID3D11Resource *back_buffer_resource);

// Called from runtime::on_present right after render_effects() has
// finished writing the effect chain to the BB. Runs the compositing
// PS and overwrites the BB with `lerp(effects_result, bb_with_ui, ui_mask)`.
void on_after_render_effects(ID3D11DeviceContext *ctx,
                             IDXGISwapChain *sc,
                             ID3D11Resource *back_buffer_resource);

// Called from runtime::on_present at the end of every frame, so
// the next frame starts with `scene_clean_captured = false` and a
// zeroed BB-draw counter.
void on_present_end();

// Called when the swapchain is being resized or destroyed. Drops
// every per-resolution resource so the next on_present recreates
// them at the new size/format.
void on_swapchain_invalidate();

// Runtime toggle (driven by the overlay UI). When `false`, every
// `on_*` becomes a near-noop and no compositing pass is submitted.
bool enabled();
void set_enabled(bool on);

} // namespace mariusfx::ui_safe_mask
