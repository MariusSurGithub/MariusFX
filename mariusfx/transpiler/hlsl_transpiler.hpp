/**
 * MariusFX — HLSL Transpiler
 * 
 * Converts ReShade pixel shaders to compute shaders for pipeline injection.
 * 
 * Transformations:
 * - PS entry(SV_Position, TEXCOORD) → CS entry(SV_DispatchThreadID)
 * - tex2D(sampler, uv) → Texture.Load(int3(pixel, 0))
 * - return color → UAV[pixel] = color
 * - Preserve uniforms, cbuffers, and helper functions
 * 
 * The transpiler aims for **semantic equivalence**, not 1:1 code mapping.
 */

#pragma once

#include "fx_parser.hpp"
#include "shader_classifier.hpp"
#include <string>

namespace mariusfx::transpiler {

// ─── Transpiler Options ──────────────────────────────────────────────────────

struct TranspilerOptions
{
    bool optimize_loads = true;         // Use Texture.Load instead of Sample when possible
    bool preserve_comments = false;     // Keep original comments in output
    bool add_debug_markers = true;      // Add [MariusFX] comments for debugging
    int thread_group_size_x = 8;        // Compute shader thread group size
    int thread_group_size_y = 8;
};

// ─── Transpiled Shader ───────────────────────────────────────────────────────

struct TranspiledShader
{
    bool is_pixel_shader = false;       // true = native PS, false = transpiled CS
    std::string compute_shader_source;  // Full HLSL (PS or CS)
    std::string entry_point;            // Entry point name (e.g., "CS_Main" or "PS_Main")
    
    // Resource bindings (for D3D11 setup)
    struct Binding {
        std::string name;
        int slot;
        enum Type { SRV, UAV, CBUFFER, SAMPLER } type;
    };
    std::vector<Binding> bindings;
    
    // Dispatch dimensions (computed from backbuffer size)
    // For CS: num_groups_x = ceil(width / thread_group_size_x)
    // For PS: ignored (fullscreen quad)
    int thread_group_size_x;
    int thread_group_size_y;
    
    // Original shader info (for debugging)
    std::string original_filename;
    std::string original_technique;
    int original_pass_index;
    
    bool success = false;
    std::string error_message;
};

// ─── Transpiler API ──────────────────────────────────────────────────────────

/**
 * Transpile a single pixel shader pass to a compute shader.
 * 
 * @param fx            Parsed .fx file
 * @param technique_idx Index of the technique to transpile
 * @param pass_idx      Index of the pass within the technique
 * @param classification Shader classification (for context-aware transpilation)
 * @param options       Transpiler options
 * @return Transpiled compute shader, or error
 */
TranspiledShader transpile_pass(
    const ParsedFX &fx,
    size_t technique_idx,
    size_t pass_idx,
    const ShaderClassification &classification,
    const TranspilerOptions &options = {}
);

/**
 * Transpile an entire technique (all passes).
 * 
 * Multi-pass techniques are converted to multiple compute shader dispatches.
 * 
 * @return Vector of transpiled shaders (one per pass)
 */
std::vector<TranspiledShader> transpile_technique(
    const ParsedFX &fx,
    size_t technique_idx,
    const ShaderClassification &classification,
    const TranspilerOptions &options = {}
);

/**
 * Quick test: check if a pixel shader is transpilable.
 * 
 * Some shaders use features that are hard to transpile (e.g., ddx/ddy,
 * SampleGrad, etc.). This function does a quick check without full transpilation.
 * 
 * @return true if likely transpilable, false if definitely not
 */
bool is_transpilable(const std::string &pixel_shader_source);

} // namespace mariusfx::transpiler
