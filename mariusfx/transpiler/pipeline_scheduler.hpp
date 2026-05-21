/**
 * MariusFX — Pipeline Injection Scheduler
 * 
 * Manages the execution order of transpiled shaders in the RAGE pipeline.
 * 
 * Responsibilities:
 * - Sort shaders by priority
 * - Inject at the correct pipeline stage
 * - Manage resource dependencies (GBuffer, depth, etc.)
 * - Handle multi-pass techniques
 * - Provide runtime enable/disable per shader
 */

#pragma once

#include "hlsl_transpiler.hpp"
#include "shader_classifier.hpp"
#include <d3d11.h>
#include <vector>
#include <string>
#include <memory>

namespace mariusfx::transpiler {

// ─── Injection Point ─────────────────────────────────────────────────────────

enum class InjectionPoint
{
    AFTER_GBUFFER,      // After RAGE GBuffer pass, before lighting
    BEFORE_TONEMAP,     // After lighting, before tonemap (HDR)
    AFTER_TONEMAP,      // After tonemap, before post-FX (LDR)
    AFTER_AA,           // After anti-aliasing
    BEFORE_UI,          // Before UI rendering (final cosmetic pass)
};

// ─── Scheduled Shader ────────────────────────────────────────────────────────

struct ScheduledShader
{
    std::string name;                   // User-facing name (e.g., "MXAO")
    std::string original_fx_path;       // Path to original .fx file
    
    ShaderType type;
    int priority;
    InjectionPoint injection_point;
    
    // Transpiled compute shader(s)
    std::vector<TranspiledShader> passes;
    
    // D3D11 resources (compiled shaders, constant buffers, etc.)
    struct CompiledPass {
        bool is_pixel_shader = false;  // true = PS, false = CS
        union {
            ID3D11ComputeShader *compute_shader;
            ID3D11PixelShader *pixel_shader;
        } shader_ptr = { nullptr };
        ID3D11Buffer *cbuffer = nullptr;
        std::vector<ID3D11ShaderResourceView*> srvs;
        std::vector<ID3D11UnorderedAccessView*> uavs;
        std::vector<ID3D11SamplerState*> samplers;
    };
    std::vector<CompiledPass> compiled_passes;
    
    // Runtime state
    bool enabled = true;
    bool compiled = false;
    bool failed = false;
    std::string error_message;
    
    // Performance stats
    double last_dispatch_time_ms = 0.0;
    uint64_t total_dispatches = 0;
};

// ─── Pipeline Scheduler ──────────────────────────────────────────────────────

class PipelineScheduler
{
public:
    /**
     * Initialize the scheduler.
     * 
     * @param device D3D11 device (must remain valid)
     */
    bool initialize(ID3D11Device *device);
    
    /**
     * Shutdown and release all resources.
     */
    void shutdown();
    
    /**
     * Register a transpiled shader for injection.
     * 
     * The shader will be compiled and added to the execution queue.
     * 
     * @param fx            Original parsed .fx
     * @param technique_idx Technique index
     * @param classification Shader classification
     * @return true if successfully registered
     */
    bool register_shader(
        const ParsedFX &fx,
        size_t technique_idx,
        const ShaderClassification &classification
    );
    
    /**
     * Execute all shaders at a specific injection point.
     * 
     * Called by gbuffer_capture hooks at the appropriate pipeline stage.
     * 
     * @param ctx D3D11 context
     * @param point Injection point
     * @param width Backbuffer width
     * @param height Backbuffer height
     */
    void execute_at_point(
        ID3D11DeviceContext *ctx,
        InjectionPoint point,
        uint32_t width,
        uint32_t height
    );
    
    /**
     * Enable or disable a specific shader.
     * 
     * @param name Shader name (e.g., "MXAO")
     * @param enabled true to enable, false to disable
     */
    void set_shader_enabled(const std::string &name, bool enabled);
    
    /**
     * Get all registered shaders (for UI display).
     */
    const std::vector<std::shared_ptr<ScheduledShader>>& get_shaders() const { return m_shaders; }
    
    /**
     * Reload a specific shader (recompile from source).
     * 
     * Useful for hot-reload during development.
     */
    bool reload_shader(const std::string &name);
    
private:
    ID3D11Device *m_device = nullptr;
    std::vector<std::shared_ptr<ScheduledShader>> m_shaders;
    
    // Shaders sorted by injection point for fast lookup
    std::vector<ScheduledShader*> m_shaders_by_point[5]; // One per InjectionPoint
    
    bool compile_shader(ScheduledShader *shader);
    void sort_shaders();
    InjectionPoint priority_to_injection_point(int priority);
};

// ─── Global Scheduler Instance ───────────────────────────────────────────────

/**
 * Get the global pipeline scheduler instance.
 */
PipelineScheduler& get_scheduler();

} // namespace mariusfx::transpiler
