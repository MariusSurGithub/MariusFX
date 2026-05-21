/**
 * MariusFX — FX Shader Parser
 * 
 * Parses ReShade .fx files (HLSL + annotations) and extracts:
 * - Techniques and passes
 * - Pixel shader entry points
 * - Uniforms and textures
 * - Sampler states
 * 
 * This is the first step of the transpiler pipeline.
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace mariusfx::transpiler {

// ─── Data Structures ─────────────────────────────────────────────────────────

struct Uniform
{
    std::string name;
    std::string type;           // float, float2, int, bool, etc.
    std::string default_value;
    
    // UI annotations (optional)
    std::string ui_label;
    std::string ui_tooltip;
    std::string ui_type;        // slider, drag, combo, etc.
    float ui_min = 0.0f;
    float ui_max = 1.0f;
    float ui_step = 0.01f;
};

struct Texture
{
    std::string name;
    int width = 0;              // 0 = BUFFER_WIDTH
    int height = 0;             // 0 = BUFFER_HEIGHT
    std::string format;         // RGBA8, RGBA16F, R32F, etc.
    int mip_levels = 1;
};

struct Sampler
{
    std::string name;
    std::string texture;        // Associated texture name
    std::string address_u;      // CLAMP, WRAP, MIRROR, BORDER
    std::string address_v;
    std::string filter;         // LINEAR, POINT, ANISOTROPIC
};

struct Pass
{
    std::string vertex_shader;  // Entry point name
    std::string pixel_shader;   // Entry point name
    
    // Render state (optional)
    bool blend_enable = false;
    std::string blend_op;
    std::string src_blend;
    std::string dest_blend;
    
    bool stencil_enable = false;
    int stencil_ref = 0;
    // ... etc.
};

struct Technique
{
    std::string name;
    std::vector<Pass> passes;
    
    // UI annotations (optional)
    bool enabled = false;
    int timeout = 0;            // Auto-disable after N ms
};

struct ParsedFX
{
    std::string source;         // Original HLSL source
    std::string filename;
    
    std::vector<Uniform> uniforms;
    std::vector<Texture> textures;
    std::vector<Sampler> samplers;
    std::vector<Technique> techniques;
    
    // Extracted shader functions (raw HLSL)
    std::unordered_map<std::string, std::string> shader_functions;
};

// ─── Parser API ──────────────────────────────────────────────────────────────

/**
 * Parse a .fx file and extract all metadata.
 * 
 * @param fx_source Full HLSL source code
 * @param filename  Original filename (for error messages)
 * @return Parsed structure, or empty if parse failed
 */
ParsedFX parse_fx(const std::string &fx_source, const std::string &filename);

/**
 * Extract a specific shader function from the source.
 * 
 * Example: extract_shader_function(source, "PS_Main") returns the full
 * function body including signature.
 * 
 * @param source    HLSL source
 * @param entry     Entry point name (e.g., "PS_Main")
 * @return Function source, or empty if not found
 */
std::string extract_shader_function(const std::string &source, const std::string &entry);

} // namespace mariusfx::transpiler
