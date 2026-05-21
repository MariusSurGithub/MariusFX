/**
 * MariusFX — Bloom Injector (Header)
 * 
 * Manages the Bloom HDR compute shader injection into the RAGE pipeline.
 * This module compiles the compute shader at runtime, creates necessary
 * D3D11 resources (UAVs, temp buffers), and dispatches the shader
 * BEFORE the tonemap pass (while still in HDR).
 * 
 * Author: MariusFX Team
 * License: MIT
 */

#pragma once

#include <d3d11.h>
#include <cstdint>

namespace mariusfx::bloom_injector {

// ─── Lifecycle ───────────────────────────────────────────────────────────────

/**
 * Initialize the Bloom injector.
 * Compiles the compute shader from embedded HLSL source.
 * 
 * @param device D3D11 device (must remain valid for the lifetime of the injector)
 * @return true if initialization succeeded, false otherwise
 */
bool initialize(ID3D11Device *device);

/**
 * Shutdown the Bloom injector.
 * Releases all D3D11 resources (shader, UAVs, temp buffers).
 */
void shutdown();

/**
 * Check if the injector is ready to dispatch.
 * 
 * @return true if initialized and shader compiled successfully
 */
bool is_ready();

// ─── Injection ───────────────────────────────────────────────────────────────

/**
 * Inject Bloom pass into the pipeline.
 * This should be called BEFORE the tonemap pass, while the image is in HDR.
 * 
 * Preconditions:
 * - HDR buffer must be available as a UAV-compatible texture
 * 
 * @param ctx D3D11 device context
 * @param hdr_buffer_uav UAV to HDR buffer (R16G16B16A16_FLOAT, read-write)
 * @param width Backbuffer width
 * @param height Backbuffer height
 */
void inject_bloom_pass(
    ID3D11DeviceContext *ctx,
    ID3D11UnorderedAccessView *hdr_buffer_uav,
    uint32_t width,
    uint32_t height
);

// ─── Configuration ───────────────────────────────────────────────────────────

/**
 * Bloom parameters (exposed for runtime tweaking).
 */
struct BloomParams
{
    float threshold;      // Luminance threshold (default: 1.0)
    float intensity;      // Bloom intensity (default: 0.5)
    float radius;         // Blur radius multiplier (default: 1.0)
};

/**
 * Get current Bloom parameters.
 * 
 * @return Reference to current parameters (can be modified)
 */
BloomParams& get_params();

/**
 * Set Bloom parameters.
 * 
 * @param params New parameters
 */
void set_params(const BloomParams &params);

/**
 * Enable or disable Bloom injection.
 * When disabled, inject_bloom_pass() becomes a no-op.
 * 
 * @param enabled true to enable, false to disable
 */
void set_enabled(bool enabled);

/**
 * Check if Bloom injection is enabled.
 * 
 * @return true if enabled, false otherwise
 */
bool is_enabled();

} // namespace mariusfx::bloom_injector
