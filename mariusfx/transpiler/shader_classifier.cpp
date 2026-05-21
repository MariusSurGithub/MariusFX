/**
 * MariusFX — Shader Classifier Implementation
 * 
 * Automatically detects shader type using pattern matching on:
 * - Filename
 * - Uniform names
 * - Texture usage
 * - Shader source code keywords
 */

#include "shader_classifier.hpp"
#include <algorithm>
#include <cctype>

namespace mariusfx::transpiler {

// ─── Utility Functions ───────────────────────────────────────────────────────

static std::string to_lower(const std::string &str)
{
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return result;
}

static bool contains(const std::string &str, const std::string &substr)
{
    return to_lower(str).find(to_lower(substr)) != std::string::npos;
}

static bool contains_any(const std::string &str, const std::vector<std::string> &keywords)
{
    for (const auto &kw : keywords)
        if (contains(str, kw))
            return true;
    return false;
}

// ─── Type Name Mapping ───────────────────────────────────────────────────────

const char* shader_type_name(ShaderType type)
{
    switch (type)
    {
    case ShaderType::AMBIENT_OCCLUSION:         return "Ambient Occlusion";
    case ShaderType::BLOOM:                     return "Bloom";
    case ShaderType::COLOR_GRADING:             return "Color Grading";
    case ShaderType::ANTI_ALIASING:             return "Anti-Aliasing";
    case ShaderType::SHARPEN:                   return "Sharpen";
    case ShaderType::DEPTH_OF_FIELD:            return "Depth of Field";
    case ShaderType::MOTION_BLUR:               return "Motion Blur";
    case ShaderType::TONEMAPPING:               return "Tonemapping";
    case ShaderType::LENS_EFFECTS:              return "Lens Effects";
    case ShaderType::FILM_GRAIN:                return "Film Grain";
    case ShaderType::SCREEN_SPACE_REFLECTIONS:  return "Screen-Space Reflections";
    case ShaderType::GLOBAL_ILLUMINATION:       return "Global Illumination";
    case ShaderType::COSMETIC:                  return "Cosmetic";
    case ShaderType::UNKNOWN:                   return "Unknown";
    default:                                    return "Invalid";
    }
}

bool should_convert_to_compute(ShaderType type)
{
    // Some effects don't benefit from compute shader conversion
    switch (type)
    {
    case ShaderType::AMBIENT_OCCLUSION:
    case ShaderType::BLOOM:
    case ShaderType::ANTI_ALIASING:
    case ShaderType::SHARPEN:
    case ShaderType::SCREEN_SPACE_REFLECTIONS:
    case ShaderType::GLOBAL_ILLUMINATION:
        return true;  // Performance-critical, benefit from compute
    
    case ShaderType::FILM_GRAIN:
    case ShaderType::LENS_EFFECTS:
    case ShaderType::COSMETIC:
        return false;  // Simple effects, pixel shader is fine
    
    default:
        return true;  // Default: try to convert
    }
}

// ─── Classification Heuristics ───────────────────────────────────────────────

static ShaderClassification classify_by_filename(const std::string &filename)
{
    ShaderClassification result = {};  // Zero-initialize all fields
    result.confidence = 0.0f;
    result.requires_pixel_shader = false;
    
    // Ambient Occlusion
    if (contains_any(filename, {"MXAO", "SSAO", "HBAO", "GTAO", "SSDO", "XeGTAO", "LSAO", "RTAO", "AO"}))
    {
        result.type = ShaderType::AMBIENT_OCCLUSION;
        result.priority = PRIORITY_AFTER_GBUFFER;
        result.needs_depth = true;
        result.needs_gbuffer_normal = true;
        result.is_hdr = false;
        result.confidence = 0.9f;
        result.reason = "Filename contains AO keyword";
        return result;
    }
    
    // Bloom
    if (contains_any(filename, {"Bloom", "Glow", "LensFlare", "Anamorphic"}))
    {
        result.type = ShaderType::BLOOM;
        result.priority = PRIORITY_BEFORE_TONEMAP;
        result.needs_gbuffer_hdr = true;
        result.is_hdr = true;
        result.is_multi_pass = true;
        result.confidence = 0.85f;
        result.reason = "Filename contains Bloom keyword";
        return result;
    }
    
    // Anti-Aliasing
    if (contains_any(filename, {"SMAA", "FXAA", "TAA", "DLAA", "CMAA"}))
    {
        result.type = ShaderType::ANTI_ALIASING;
        result.priority = PRIORITY_ANTI_ALIASING;
        result.is_hdr = false;
        result.is_multi_pass = true;
        result.confidence = 0.95f;
        result.reason = "Filename contains AA keyword";
        return result;
    }
    
    // Color Grading
    if (contains_any(filename, {"Vibrance", "Saturation", "LUT", "ColorGrading", "Curves", "Levels", "Tonemap"}))
    {
        result.type = ShaderType::COLOR_GRADING;
        result.priority = PRIORITY_AFTER_TONEMAP;
        result.is_hdr = false;
        result.confidence = 0.8f;
        result.reason = "Filename contains color grading keyword";
        return result;
    }
    
    // Sharpen
    if (contains_any(filename, {"Sharpen", "CAS", "Unsharp", "Adaptive"}))
    {
        result.type = ShaderType::SHARPEN;
        result.priority = PRIORITY_POST_AA;
        result.is_hdr = false;
        result.confidence = 0.85f;
        result.reason = "Filename contains sharpen keyword";
        return result;
    }
    
    // Depth of Field
    if (contains_any(filename, {"DOF", "Bokeh", "Defocus"}))
    {
        result.type = ShaderType::DEPTH_OF_FIELD;
        result.priority = PRIORITY_POST_AA;
        result.needs_depth = true;
        result.is_hdr = false;
        result.confidence = 0.9f;
        result.reason = "Filename contains DOF keyword";
        return result;
    }
    
    // Screen-Space Reflections
    if (contains_any(filename, {"SSR", "Reflection"}))
    {
        result.type = ShaderType::SCREEN_SPACE_REFLECTIONS;
        result.priority = PRIORITY_AFTER_LIGHTING;
        result.needs_depth = true;
        result.needs_gbuffer_normal = true;
        result.is_hdr = true;
        result.confidence = 0.9f;
        result.reason = "Filename contains SSR keyword";
        return result;
    }
    
    // Global Illumination
    if (contains_any(filename, {"GI", "SSGI", "RTGI", "RadiantGI"}))
    {
        result.type = ShaderType::GLOBAL_ILLUMINATION;
        result.priority = PRIORITY_AFTER_GBUFFER + 10;
        result.needs_depth = true;
        result.needs_gbuffer_normal = true;
        result.needs_gbuffer_albedo = true;
        result.is_hdr = true;
        result.confidence = 0.9f;
        result.reason = "Filename contains GI keyword";
        return result;
    }
    
    // Film Grain
    if (contains_any(filename, {"Grain", "Noise", "Dither"}))
    {
        result.type = ShaderType::FILM_GRAIN;
        result.priority = PRIORITY_COSMETIC;
        result.is_hdr = false;
        result.confidence = 0.85f;
        result.reason = "Filename contains grain keyword";
        return result;
    }
    
    // Lens Effects
    if (contains_any(filename, {"Vignette", "ChromaticAberration", "Distortion", "Lens"}))
    {
        result.type = ShaderType::LENS_EFFECTS;
        result.priority = PRIORITY_COSMETIC;
        result.is_hdr = false;
        result.confidence = 0.8f;
        result.reason = "Filename contains lens effect keyword";
        return result;
    }
    
    // Motion Blur
    if (contains_any(filename, {"MotionBlur", "Velocity"}))
    {
        result.type = ShaderType::MOTION_BLUR;
        result.priority = PRIORITY_POST_AA;
        result.needs_gbuffer_motion = true;
        result.needs_temporal = true;
        result.is_hdr = false;
        result.confidence = 0.9f;
        result.reason = "Filename contains motion blur keyword";
        return result;
    }
    
    return result;  // No match, confidence = 0
}

static void refine_by_uniforms(const ParsedFX &fx, ShaderClassification &result)
{
    // Collect all uniform names
    std::string all_uniform_names;
    for (const auto &u : fx.uniforms)
        all_uniform_names += to_lower(u.name) + " ";
    
    // Ambient Occlusion indicators
    if (contains_any(all_uniform_names, {"occlusion", "ao", "ambient", "radius", "samples"}))
    {
        if (result.type == ShaderType::UNKNOWN)
        {
            result.type = ShaderType::AMBIENT_OCCLUSION;
            result.priority = PRIORITY_AFTER_GBUFFER;
            result.needs_depth = true;
            result.confidence = 0.7f;
            result.reason = "Uniform names suggest AO";
        }
        else if (result.type == ShaderType::AMBIENT_OCCLUSION)
        {
            result.confidence = std::min(1.0f, result.confidence + 0.1f);
        }
    }
    
    // Bloom indicators
    if (contains_any(all_uniform_names, {"bloom", "threshold", "intensity", "glow"}))
    {
        if (result.type == ShaderType::UNKNOWN)
        {
            result.type = ShaderType::BLOOM;
            result.priority = PRIORITY_BEFORE_TONEMAP;
            result.is_hdr = true;
            result.confidence = 0.7f;
            result.reason = "Uniform names suggest Bloom";
        }
        else if (result.type == ShaderType::BLOOM)
        {
            result.confidence = std::min(1.0f, result.confidence + 0.1f);
        }
    }
    
    // Color grading indicators
    if (contains_any(all_uniform_names, {"vibrance", "saturation", "contrast", "brightness", "gamma"}))
    {
        if (result.type == ShaderType::UNKNOWN)
        {
            result.type = ShaderType::COLOR_GRADING;
            result.priority = PRIORITY_AFTER_TONEMAP;
            result.confidence = 0.65f;
            result.reason = "Uniform names suggest color grading";
        }
        else if (result.type == ShaderType::COLOR_GRADING)
        {
            result.confidence = std::min(1.0f, result.confidence + 0.1f);
        }
    }
}

static void refine_by_shader_source(const ParsedFX &fx, ShaderClassification &result)
{
    // Get all pixel shader sources
    std::string all_shader_source;
    for (const auto &[name, source] : fx.shader_functions)
        all_shader_source += to_lower(source) + " ";
    
    // Depth usage → likely depth-dependent effect
    if (contains_any(all_shader_source, {"getlinearizeddepth", "depthbuffer", "reshade::depth"}))
    {
        result.needs_depth = true;
        
        if (result.type == ShaderType::UNKNOWN)
        {
            // Could be AO, DOF, or fog
            if (contains(all_shader_source, "occlusion") || contains(all_shader_source, "ambient"))
            {
                result.type = ShaderType::AMBIENT_OCCLUSION;
                result.priority = PRIORITY_AFTER_GBUFFER;
                result.confidence = 0.6f;
                result.reason = "Uses depth + occlusion keywords";
            }
            else if (contains(all_shader_source, "blur") || contains(all_shader_source, "bokeh"))
            {
                result.type = ShaderType::DEPTH_OF_FIELD;
                result.priority = PRIORITY_POST_AA;
                result.confidence = 0.6f;
                result.reason = "Uses depth + blur keywords";
            }
        }
    }
    
    // Multi-pass detection (downscale/upscale pattern → likely bloom)
    if (contains_any(all_shader_source, {"downsample", "upsample", "miplevel"}))
    {
        result.is_multi_pass = true;
        
        if (result.type == ShaderType::BLOOM || result.type == ShaderType::UNKNOWN)
        {
            result.type = ShaderType::BLOOM;
            result.priority = PRIORITY_BEFORE_TONEMAP;
            result.is_hdr = true;
            result.confidence = std::max(result.confidence, 0.65f);
            result.reason = "Multi-pass downscale/upscale pattern (bloom)";
        }
    }
    
    // Temporal usage
    if (contains_any(all_shader_source, {"prevframe", "history", "temporal", "accumulation"}))
    {
        result.needs_temporal = true;
    }
    
    // Pixel Shader-only features detection
    // These features CANNOT be transpiled to Compute Shader
    if (contains_any(all_shader_source, {"ddx(", "ddy(", "fwidth(", "ddx_fine(", "ddy_fine(", "ddx_coarse(", "ddy_coarse("}))
    {
        result.requires_pixel_shader = true;
        result.reason += " [Requires PS: derivatives]";
    }
    
    // Force known problematic shaders to PS mode (they use derivatives in macros/inline functions)
    if (contains_any(fx.filename, {"SSDO", "MXAO", "GTAO", "HBAO", "RTAO"}))
    {
        result.requires_pixel_shader = true;
        result.reason += " [Requires PS: AO shader with implicit derivatives]";
    }
}

// ─── Main Classifier ─────────────────────────────────────────────────────────

ShaderClassification classify_shader(const ParsedFX &fx)
{
    ShaderClassification result;
    
    // Step 1: Classify by filename (highest confidence)
    result = classify_by_filename(fx.filename);
    
    // Step 2: Refine by uniforms
    refine_by_uniforms(fx, result);
    
    // Step 3: Refine by shader source
    refine_by_shader_source(fx, result);
    
    // Step 4: Fallback for unknown shaders
    if (result.type == ShaderType::UNKNOWN)
    {
        result.type = ShaderType::COSMETIC;
        result.priority = PRIORITY_UNKNOWN;
        result.confidence = 0.3f;
        result.reason = "No clear classification, defaulting to cosmetic post-process";
    }
    
    // Step 5: Sanity checks
    if (result.confidence < 0.5f)
    {
        // Low confidence → mark as unknown, don't transpile
        result.type = ShaderType::UNKNOWN;
        result.priority = PRIORITY_UNKNOWN;
    }
    
    return result;
}

} // namespace mariusfx::transpiler
