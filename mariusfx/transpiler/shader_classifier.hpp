/**
 * MariusFX — Shader Classifier
 * 
 * Automatically detects the type of a .fx shader (AO, Bloom, Color Grading, etc.)
 * and assigns a pipeline injection priority.
 * 
 * Classification is done via pattern matching on:
 * - Shader name
 * - Uniform names
 * - Texture usage
 * - HLSL keywords in the pixel shader
 */

#pragma once

#include "fx_parser.hpp"
#include <string>

namespace mariusfx::transpiler {

// ─── Shader Types ────────────────────────────────────────────────────────────

enum class ShaderType
{
    AMBIENT_OCCLUSION,  // SSAO, HBAO, GTAO, SSDO, etc.
    BLOOM,              // HDR bloom, glow, lens flares
    COLOR_GRADING,      // LUT, vibrance, saturation, curves, levels
    ANTI_ALIASING,      // SMAA, FXAA, TAA
    SHARPEN,            // CAS, unsharp mask, adaptive sharpen
    DEPTH_OF_FIELD,     // Bokeh DOF, cinematic DOF
    MOTION_BLUR,        // Camera motion blur, per-object motion blur
    TONEMAPPING,        // Reinhard, ACES, filmic, etc.
    LENS_EFFECTS,       // Chromatic aberration, distortion, vignette
    FILM_GRAIN,         // Noise, grain, dither
    SCREEN_SPACE_REFLECTIONS, // SSR
    GLOBAL_ILLUMINATION,      // SSGI, RTGI approximations
    COSMETIC,           // Misc effects (borders, overlays, etc.)
    UNKNOWN
};

// ─── Pipeline Priorities ─────────────────────────────────────────────────────

// Lower number = earlier in pipeline
constexpr int PRIORITY_AFTER_GBUFFER        = 0;    // AO, SSGI
constexpr int PRIORITY_BEFORE_LIGHTING      = 50;   // (reserved)
constexpr int PRIORITY_AFTER_LIGHTING       = 100;  // SSR
constexpr int PRIORITY_BEFORE_TONEMAP       = 150;  // Bloom (HDR)
constexpr int PRIORITY_TONEMAP              = 200;  // Custom tonemaps
constexpr int PRIORITY_AFTER_TONEMAP        = 250;  // Color grading (LDR)
constexpr int PRIORITY_ANTI_ALIASING        = 300;  // SMAA, FXAA
constexpr int PRIORITY_POST_AA              = 350;  // Sharpen, DOF
constexpr int PRIORITY_COSMETIC             = 400;  // Grain, vignette, etc.
constexpr int PRIORITY_BEFORE_UI            = 900;  // Final pass before UI
constexpr int PRIORITY_UNKNOWN              = 999;  // Fallback (post-process)

// ─── Classification Result ───────────────────────────────────────────────────

struct ShaderClassification
{
    ShaderType type;
    int priority;               // Pipeline injection priority (0-999)
    
    // Resource requirements
    bool needs_gbuffer_hdr;
    bool needs_gbuffer_albedo;
    bool needs_gbuffer_normal;
    bool needs_gbuffer_specular;
    bool needs_gbuffer_motion;
    bool needs_depth;
    
    // Execution context
    bool is_hdr;                // Operates in HDR (before tonemap)
    bool is_multi_pass;         // Requires multiple passes
    bool needs_temporal;        // Requires previous frame data
    bool requires_pixel_shader; // Requires PS-only features (derivatives, etc.) - cannot transpile to CS
    
    // Confidence (0.0-1.0)
    float confidence;           // How sure we are about the classification
    
    // Debug info
    std::string reason;         // Why we classified it this way
};

// ─── Classifier API ──────────────────────────────────────────────────────────

/**
 * Classify a parsed .fx shader.
 * 
 * Uses heuristics to detect the shader type and assign a pipeline priority.
 * 
 * @param fx Parsed shader structure
 * @return Classification result
 */
ShaderClassification classify_shader(const ParsedFX &fx);

/**
 * Get a human-readable name for a shader type.
 */
const char* shader_type_name(ShaderType type);

/**
 * Check if a shader type should be converted to compute shader.
 * 
 * Some effects (like cosmetic overlays) don't benefit from compute shader
 * conversion and should stay as pixel shaders.
 */
bool should_convert_to_compute(ShaderType type);

} // namespace mariusfx::transpiler
