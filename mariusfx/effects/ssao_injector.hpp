/**
 * MariusFX — SSAO Injector (Header)
 * 
 * Manages the SSAO compute shader injection into the RAGE pipeline.
 * This module compiles the compute shader at runtime, creates necessary
 * D3D11 resources (UAVs, constant buffers), and dispatches the shader
 * at the right moment (after GBuffer pass, before lighting).
 * 
 * Author: MariusFX Team
 * License: MIT
 */

#pragma once

#include <d3d11.h>
#include <cstdint>

namespace mariusfx::ssao_injector {

// ─── Lifecycle ───────────────────────────────────────────────────────────────

/**
 * Initialize the SSAO injector.
 * Compiles the compute shader from embedded HLSL source.
 * 
 * @param device D3D11 device (must remain valid for the lifetime of the injector)
 * @return true if initialization succeeded, false otherwise
 */
bool initialize(ID3D11Device *device);

/**
 * Shutdown the SSAO injector.
 * Releases all D3D11 resources (shader, UAVs, constant buffer).
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
 * Inject SSAO pass into the pipeline.
 * This should be called AFTER the GBuffer pass ends, BEFORE lighting.
 * 
 * Preconditions:
 * - GBuffer textures must be captured (Normal, Depth)
 * - HDR buffer must be available as a UAV-compatible texture
 * 
 * @param ctx D3D11 device context
 * @param gbuffer_normal_srv SRV to GBuffer Normal texture (R10G10B10A2_UNORM)
 * @param depth_srv SRV to Depth buffer (R32_FLOAT or D24_UNORM_S8_UINT)
 * @param hdr_buffer_uav UAV to HDR buffer (R16G16B16A16_FLOAT, read-write)
 * @param width Backbuffer width
 * @param height Backbuffer height
 */
void inject_ssao_pass(
    ID3D11DeviceContext *ctx,
    ID3D11ShaderResourceView *gbuffer_normal_srv,
    ID3D11ShaderResourceView *depth_srv,
    ID3D11UnorderedAccessView *hdr_buffer_uav,
    uint32_t width,
    uint32_t height
);

// ─── Configuration ───────────────────────────────────────────────────────────

/**
 * SSAO parameters (exposed for runtime tweaking).
 */
struct SSAOParams
{
    float sample_radius;      // World-space radius (default: 0.5)
    float intensity;          // AO intensity multiplier (default: 1.5)
    uint32_t sample_count;    // Samples per pixel (default: 8, max: 16)
    float depth_fade_start;   // Depth where AO starts fading (default: 0.9)
    float depth_fade_end;     // Depth where AO is fully faded (default: 0.99)
    float bias_angle;         // Bias to avoid self-occlusion (default: 0.1 radians)
};

/**
 * Get current SSAO parameters.
 * 
 * @return Reference to current parameters (can be modified)
 */
SSAOParams& get_params();

/**
 * Set SSAO parameters.
 * 
 * @param params New parameters
 */
void set_params(const SSAOParams &params);

/**
 * Enable or disable SSAO injection.
 * When disabled, inject_ssao_pass() becomes a no-op.
 * 
 * @param enabled true to enable, false to disable
 */
void set_enabled(bool enabled);

/**
 * Check if SSAO injection is enabled.
 * 
 * @return true if enabled, false otherwise
 */
bool is_enabled();

} // namespace mariusfx::ssao_injector
