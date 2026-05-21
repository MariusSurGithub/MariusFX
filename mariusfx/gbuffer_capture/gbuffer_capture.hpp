/*
 * MariusFX — RAGE GBuffer Capture
 *
 * Intercepts OMSetRenderTargets in the D3D11 thunk layer to detect the
 * deferred GBuffer fill pass (identified by >= 4 MRTs at backbuffer
 * resolution). When the pass ends (MRT count drops back), copies the
 * GBuffer textures to staging SRVs that ReShade effects can sample via
 * texture semantic bindings:
 *
 *   texture myNormals : RAGEGBufferNormal;   // per-pixel native normals
 *   texture myAlbedo  : RAGEGBufferAlbedo;   // diffuse albedo
 *   texture myHDR     : RAGEGBufferHDR;      // pre-tonemap HDR color
 *   texture mySpec    : RAGEGBufferSpecular;  // specular / material
 *   texture myMotion  : RAGEGBufferMotion;    // motion vectors
 *
 * Usage in on_present (runtime.cpp):
 *   mariusfx::gbuffer_capture::bind_to_runtime(runtime_ptr);
 *   // ... then render_effects() can access the GBuffer textures
 *   mariusfx::gbuffer_capture::on_present_end();
 *
 * Cost: one CopyResource per captured buffer per frame (~0.15ms at 1080p
 * for 5 buffers on a mid-range GPU). Copies only happen if at least one
 * loaded .fx shader references the corresponding semantic.
 */

#pragma once

#include <d3d11.h>
#include <dxgi.h>

// Forward declaration — avoids pulling in the full reshade runtime header.
namespace reshade { class runtime; }

namespace mariusfx::gbuffer_capture {

// ─── Hook callbacks ──────────────────────────────────────────────────────────

// Called from D3D11DeviceContext::OMSetRenderTargets AFTER the original call.
// Detects GBuffer MRT bindings and triggers a copy when the pass ends.
void on_omset_rt(ID3D11DeviceContext *ctx,
                 UINT num_views,
                 ID3D11RenderTargetView *const *rtvs,
                 ID3D11DepthStencilView *dsv);

// Called from D3D11DeviceContext::Draw* AFTER the original call.
// Tracks whether we are inside the GBuffer fill pass (draw count).
void on_draw(ID3D11DeviceContext *ctx);

// ─── Per-frame lifecycle ─────────────────────────────────────────────────────

// Called from runtime::on_present BEFORE render_effects.
// Binds the captured SRVs to the runtime's semantic texture map so that
// any .fx shader declaring `texture X : RAGEGBufferNormal;` receives them.
void bind_to_runtime(reshade::runtime *rt);

// Called from runtime::on_present at the END of the frame.
// Resets per-frame tracking state for the next frame.
void on_present_end();

// Called on swapchain resize/destroy — releases all GPU resources.
void on_swapchain_invalidate();

// ─── Configuration ───────────────────────────────────────────────────────────

// Master enable toggle (driven from the UI overlay).
bool enabled();
void set_enabled(bool on);

// Debug: returns the number of GBuffer MRT bindings detected this frame.
int  debug_mrt_bindings_this_frame();

// Debug: returns the backbuffer dimensions the module matched against.
void debug_get_bb_size(UINT &w, UINT &h);

// Stores the backbuffer dimensions (called once at init or resize).
void set_backbuffer_size(UINT w, UINT h);

} // namespace mariusfx::gbuffer_capture
