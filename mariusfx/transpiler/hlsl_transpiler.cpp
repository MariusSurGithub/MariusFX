/**
 * MariusFX — HLSL Transpiler Implementation
 * 
 * Converts ReShade pixel shaders to compute shaders.
 * 
 * This is NOT a full HLSL compiler — it's a source-to-source transpiler
 * that handles the most common ReShade patterns.
 */

#include "hlsl_transpiler.hpp"
#include <regex>
#include <sstream>
#include <algorithm>

namespace mariusfx::transpiler {

// ─── Utility Functions ───────────────────────────────────────────────────────

static std::string trim(const std::string &str)
{
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

static std::string indent(const std::string &str, int level)
{
    std::string prefix(level * 4, ' ');
    std::string result;
    std::istringstream iss(str);
    std::string line;
    while (std::getline(iss, line))
    {
        if (!line.empty())
            result += prefix + line + "\n";
        else
            result += "\n";
    }
    return result;
}

// ─── Source Cleanup ──────────────────────────────────────────────────────────

// Helper to strip balanced angle brackets < ... > including nested (for uniforms)
static std::string strip_angle_bracket_block(const std::string &source, const std::string &keyword_regex)
{
    std::string result;
    result.reserve(source.size());
    std::regex re(keyword_regex);
    
    size_t pos = 0;
    while (pos < source.size())
    {
        std::smatch match;
        std::string remaining = source.substr(pos);
        if (std::regex_search(remaining, match, re))
        {
            // Append everything before match
            result.append(source, pos, match.position(0));
            size_t match_start = pos + match.position(0);
            size_t match_end = match_start + match.length(0);
            
            // The opening < is the LAST char of the match
            int depth = 1;
            size_t bracket_end = match_end;
            while (bracket_end < source.size() && depth > 0)
            {
                if (source[bracket_end] == '<') depth++;
                else if (source[bracket_end] == '>') depth--;
                bracket_end++;
            }
            
            // Skip to semicolon
            while (bracket_end < source.size() && source[bracket_end] != ';') bracket_end++;
            if (bracket_end < source.size()) bracket_end++; // skip semicolon
            
            result += "/* uniform removed */";
            pos = bracket_end;
        }
        else
        {
            result.append(source, pos, std::string::npos);
            break;
        }
    }
    return result;
}

// Helper to strip balanced braces { ... } including nested
// Note: keyword_regex must match the keyword INCLUDING the opening brace
static std::string strip_balanced_block(const std::string &source, const std::string &keyword_regex)
{
    std::string result;
    result.reserve(source.size());
    std::regex re(keyword_regex);
    
    size_t pos = 0;
    while (pos < source.size())
    {
        std::smatch match;
        std::string remaining = source.substr(pos);
        if (std::regex_search(remaining, match, re))
        {
            // Append everything before match
            result.append(source, pos, match.position(0));
            size_t match_start = pos + match.position(0);
            size_t match_end = match_start + match.length(0);
            
            // The opening brace is the LAST char of the match
            // So depth starts at 1 (one open brace already consumed)
            int depth = 1;
            size_t brace_end = match_end;
            while (brace_end < source.size() && depth > 0)
            {
                if (source[brace_end] == '{') depth++;
                else if (source[brace_end] == '}') depth--;
                brace_end++;
            }
            
            // Skip optional semicolon
            if (brace_end < source.size() && source[brace_end] == ';') brace_end++;
            
            result += "/* block removed */";
            pos = brace_end;
        }
        else
        {
            result.append(source, pos, std::string::npos);
            break;
        }
    }
    return result;
}

// Helper to extract namespace content (keep inner code, remove namespace wrapper)
static std::string flatten_namespaces(const std::string &source)
{
    std::string result;
    result.reserve(source.size());
    
    std::regex ns_regex("namespace\\s+\\w+\\s*\\{");
    size_t pos = 0;
    
    while (pos < source.size())
    {
        std::smatch match;
        std::string remaining = source.substr(pos);
        if (std::regex_search(remaining, match, ns_regex))
        {
            // Append everything before namespace keyword
            result.append(source, pos, match.position(0));
            size_t ns_end = pos + match.position(0) + match.length(0);
            
            // Find matching closing brace
            int depth = 1;
            size_t brace_end = ns_end;
            while (brace_end < source.size() && depth > 0)
            {
                if (source[brace_end] == '{') depth++;
                else if (source[brace_end] == '}') depth--;
                brace_end++;
            }
            
            // Keep inner content (between ns_end and brace_end-1)
            if (brace_end > ns_end)
            {
                result.append(source, ns_end, brace_end - ns_end - 1);
            }
            pos = brace_end;
        }
        else
        {
            result.append(source, pos, std::string::npos);
            break;
        }
    }
    return result;
}

static std::string strip_reshade_annotations(const std::string &source)
{
    std::string result = source;
    
    // Remove entire uniform declarations - handle multi-line with < ... >
    // First pass: uniforms with multi-line < ... > (strip the whole block)
    result = strip_angle_bracket_block(result, "uniform\\s+\\w+(?:\\d+)?\\s+\\w+\\s*<");
    
    // Second pass: simple uniforms without < >
    std::regex uniform_simple_regex("uniform\\s+\\w+(?:\\d+)?\\s+\\w+\\s*(?:=\\s*[^;]+)?\\s*;");
    result = std::regex_replace(result, uniform_simple_regex, "// uniform removed");
    
    // Remove texture declarations (texture, texture2D, texture3D, textureCube)
    result = strip_balanced_block(result, "(?:^|\\s)texture(?:2D|3D|Cube)?\\s+\\w+\\s*<[^>]*>\\s*\\{");
    result = strip_balanced_block(result, "(?:^|\\s)texture(?:2D|3D|Cube)?\\s+\\w+\\s*\\{");
    
    // Remove sampler declarations (sampler, sampler2D, sampler3D, samplerCube, sampler_state)
    result = strip_balanced_block(result, "(?:^|\\s)sampler(?:2D|3D|Cube|_state)?\\s+\\w+\\s*\\{");
    
    // Remove technique/pass blocks (NOT valid HLSL)
    result = strip_balanced_block(result, "\\btechnique\\d*\\s+\\w+\\s*(?:<[^>]*>)?\\s*\\{");
    result = strip_balanced_block(result, "\\btechnique\\d*\\s*\\{");
    
    // Remove ALL shader function definitions (float4 FuncName(...) : SEMANTIC { ... })
    // These use 'in'/'out' keywords and will be transpiled separately
    // Match: return_type function_name ( params ) : semantic { body }
    result = strip_balanced_block(result, "\\b(?:float|float2|float3|float4|void|int|uint|bool)\\s+\\w+\\s*\\([^)]*\\)\\s*:\\s*(?:SV_Target|SV_POSITION|COLOR|TEXCOORD|DEPTH)\\s*\\{");
    result = strip_balanced_block(result, "\\b(?:float|float2|float3|float4|void|int|uint|bool)\\s+\\w+\\s*\\([^)]*\\)\\s*:\\s*(?:SV_Target|SV_POSITION|COLOR|TEXCOORD|DEPTH)\\d*\\s*\\{");
    
    // Flatten namespaces (extract inner content)
    result = flatten_namespaces(result);
    
    // Remove ui_* identifiers
    std::regex ui_keyword_regex("\\b(ui_type|ui_label|ui_tooltip|ui_min|ui_max|ui_step|ui_items|ui_category|ui_spacing|ui_text)\\b");
    result = std::regex_replace(result, ui_keyword_regex, "/* $1 */");
    
    // Remove __UNIFORM_* macros
    std::regex uniform_macro_regex("\\b__UNIFORM_[A-Z0-9_]+\\b");
    result = std::regex_replace(result, uniform_macro_regex, "/* uniform macro */");
    
    return result;
}

static std::string replace_reshade_namespace_refs(const std::string &source)
{
    std::string result = source;
    
    // Replace all ReShade:: namespace references
    result = std::regex_replace(result, std::regex("ReShade\\s*::\\s*BackBuffer"), "_ReShadeBackBuffer");
    result = std::regex_replace(result, std::regex("ReShade\\s*::\\s*GetLinearizedDepth"), "GetLinearizedDepth");
    result = std::regex_replace(result, std::regex("ReShade\\s*::\\s*ScreenSize"), "BUFFER_SCREEN_SIZE");
    result = std::regex_replace(result, std::regex("ReShade\\s*::\\s*PixelSize"), "BUFFER_PIXEL_SIZE");
    result = std::regex_replace(result, std::regex("ReShade\\s*::\\s*AspectRatio"), "BUFFER_ASPECT_RATIO");
    
    return result;
}

// ─── Transpilability Check ───────────────────────────────────────────────────

bool is_transpilable(const std::string &pixel_shader_source)
{
    // Check for features that are incompatible with compute shaders
    
    // ddx/ddy (screen-space derivatives) → NOT supported in CS
    if (pixel_shader_source.find("ddx") != std::string::npos ||
        pixel_shader_source.find("ddy") != std::string::npos ||
        pixel_shader_source.find("fwidth") != std::string::npos)
    {
        return false;
    }
    
    // SampleGrad → NOT supported in CS (use SampleLevel instead)
    if (pixel_shader_source.find("SampleGrad") != std::string::npos)
    {
        return false;
    }
    
    // Geometry shader → N/A
    if (pixel_shader_source.find("GS_") != std::string::npos)
    {
        return false;
    }
    
    return true;
}

// ─── Signature Conversion ────────────────────────────────────────────────────

struct ParsedSignature
{
    std::string return_type;
    std::string function_name;
    std::vector<std::pair<std::string, std::string>> params;  // (type, name)
    std::string semantic;  // e.g., "SV_Target"
};

static ParsedSignature parse_ps_signature(const std::string &ps_function)
{
    ParsedSignature sig;
    
    // Pattern: RETURN_TYPE FUNCTION_NAME(PARAMS) : SEMANTIC
    std::regex sig_regex("(\\w+)\\s+(\\w+)\\s*\\(([^)]*)\\)\\s*(?::\\s*(\\w+))?");
    std::smatch match;
    
    if (std::regex_search(ps_function, match, sig_regex))
    {
        sig.return_type = match[1].str();
        sig.function_name = match[2].str();
        std::string params_str = match[3].str();
        sig.semantic = match[4].str();
        
        // Parse parameters
        std::regex param_regex("(\\w+(?:\\d+)?)\\s+(\\w+)\\s*(?::\\s*\\w+)?");
        auto begin = std::sregex_iterator(params_str.begin(), params_str.end(), param_regex);
        auto end = std::sregex_iterator();
        
        for (auto it = begin; it != end; ++it)
        {
            sig.params.push_back({(*it)[1].str(), (*it)[2].str()});
        }
    }
    
    return sig;
}

static std::string generate_cs_signature(const ParsedSignature &ps_sig, const TranspilerOptions &opts)
{
    std::ostringstream oss;
    
    // Compute shader signature
    oss << "[numthreads(" << opts.thread_group_size_x << ", " << opts.thread_group_size_y << ", 1)]\n";
    oss << "void CS_" << ps_sig.function_name << "(uint3 DTid : SV_DispatchThreadID)\n";
    
    return oss.str();
}

// ─── Body Conversion ─────────────────────────────────────────────────────────

static std::string convert_ps_body_to_cs(
    const std::string &ps_body,
    const ParsedSignature &ps_sig,
    const TranspilerOptions &opts)
{
    std::ostringstream oss;
    
    // Extract function body (between { and })
    size_t body_start = ps_body.find('{');
    size_t body_end = ps_body.rfind('}');
    if (body_start == std::string::npos || body_end == std::string::npos)
        return "";
    
    std::string body = ps_body.substr(body_start + 1, body_end - body_start - 1);
    
    // Add compute shader preamble
    oss << "{\n";
    oss << "    // [MariusFX Transpiler] Converted from PS_" << ps_sig.function_name << "\n";
    oss << "    uint2 pixel = DTid.xy;\n";
    oss << "    \n";
    
    // Convert parameters to local variables
    for (const auto &[type, name] : ps_sig.params)
    {
        if (name == "vpos" || name == "pos" || name == "position")
        {
            // SV_Position → pixel coordinates
            oss << "    float4 " << name << " = float4(pixel, 0.0, 1.0);\n";
        }
        else if (name == "texcoord" || name == "uv" || name == "tc")
        {
            // TEXCOORD → normalized UV
            oss << "    float2 " << name << " = (pixel + 0.5) / float2(g_screen_width, g_screen_height);\n";
        }
        else
        {
            // Unknown parameter → leave as-is (will likely cause compile error)
            oss << "    " << type << " " << name << "; // TODO: initialize\n";
        }
    }
    
    oss << "    \n";
    
    // Convert body
    std::string converted_body = body;
    
    // 1. Convert tex2D(sampler, uv) → Texture.Load(int3(pixel, 0))
    //    This is a simplification — we assume point sampling is OK
    //    TODO: Handle SampleLevel for filtered sampling
    std::regex tex2d_regex("tex2D\\s*\\(\\s*(\\w+)\\s*,\\s*([^)]+)\\)");
    converted_body = std::regex_replace(converted_body, tex2d_regex,
        "tex$1.Load(int3(pixel, 0))  // [MariusFX] Converted from tex2D");
    
    // 2. Convert return COLOR; → uavOutput[pixel] = float4_auto(COLOR); return;
    // float4_auto handles float/float2/float3/float4 automatically
    std::regex return_regex("return\\s+([^;]+);");
    converted_body = std::regex_replace(converted_body, return_regex,
        "uavOutput[pixel] = float4_auto($1); return;  // [MariusFX] Converted from return");
    
    // 3. Convert ReShade::GetLinearizedDepth(uv) → linearize_depth(texDepth.Load(int3(pixel, 0)).r)
    std::regex depth_regex("ReShade::GetLinearizedDepth\\s*\\([^)]+\\)");
    converted_body = std::regex_replace(converted_body, depth_regex,
        "linearize_depth(texDepth.Load(int3(pixel, 0)).r)");
    
    oss << indent(converted_body, 1);
    oss << "}\n";
    
    return oss.str();
}

// ─── Resource Binding Generation ────────────────────────────────────────────

static std::vector<TranspiledShader::Binding> generate_bindings(const ParsedFX &fx)
{
    std::vector<TranspiledShader::Binding> bindings;
    
    int srv_slot = 0;
    int uav_slot = 0;
    int sampler_slot = 0;
    
    // Textures → SRVs
    for (const auto &tex : fx.textures)
    {
        bindings.push_back({tex.name, srv_slot++, TranspiledShader::Binding::SRV});
    }
    
    // Samplers
    for (const auto &samp : fx.samplers)
    {
        bindings.push_back({samp.name, sampler_slot++, TranspiledShader::Binding::SAMPLER});
    }
    
    // Output UAV (always slot 0)
    bindings.push_back({"uavOutput", 0, TranspiledShader::Binding::UAV});
    
    // Constant buffer (always slot 0)
    bindings.push_back({"CB_Params", 0, TranspiledShader::Binding::CBUFFER});
    
    return bindings;
}

// ─── Constant Buffer Generation ─────────────────────────────────────────────

static std::string generate_cbuffer(const ParsedFX &fx)
{
    std::ostringstream oss;
    
    oss << "cbuffer CB_Params : register(b0)\n";
    oss << "{\n";
    
    // Add all uniforms
    for (const auto &u : fx.uniforms)
    {
        oss << "    " << u.type << " " << u.name << ";\n";
    }
    
    // Add screen size (always needed)
    oss << "    uint g_screen_width;\n";
    oss << "    uint g_screen_height;\n";
    
    oss << "};\n\n";
    
    return oss.str();
}

// ─── Texture Declarations ───────────────────────────────────────────────────

static std::string generate_texture_declarations(const ParsedFX &fx)
{
    std::ostringstream oss;
    
    int srv_slot = 0;
    
    // ReShade BackBuffer (always slot 0)
    oss << "// ReShade compatibility - BackBuffer texture\n";
    oss << "Texture2D<float4> _ReShadeBackBuffer : register(t" << srv_slot++ << ");\n";
    oss << "#define BackBuffer _ReShadeBackBuffer\n\n";
    
    // Input textures
    for (const auto &tex : fx.textures)
    {
        // Guess format from name
        std::string format = "float4";
        if (tex.format == "R8" || tex.format == "R16F" || tex.format == "R32F")
            format = "float";
        else if (tex.format == "RG8" || tex.format == "RG16F" || tex.format == "RG32F")
            format = "float2";
        
        oss << "Texture2D<" << format << "> " << tex.name << " : register(t" << srv_slot++ << ");\n";
    }
    
    oss << "\n";
    
    // Output UAV
    oss << "RWTexture2D<float4> uavOutput : register(u0);\n\n";
    
    return oss.str();
}

// ─── Sampler Declarations ───────────────────────────────────────────────────

static std::string generate_sampler_declarations(const ParsedFX &fx)
{
    std::ostringstream oss;
    
    oss << "// Auto-generated samplers for tex2D compatibility\n";
    
    // Generate sampler for BackBuffer
    oss << "SamplerState _ReShadeBackBufferSampler : register(s0);\n";
    
    int sampler_slot = 1;
    
    // Generate sampler for each texture
    for (const auto &tex : fx.textures)
    {
        oss << "SamplerState " << tex.name << "Sampler : register(s" << sampler_slot++ << ");\n";
    }
    
    // Generate samplers from parsed sampler declarations
    // Each sampler also needs a corresponding Texture2D alias since
    // ReShade uses tex2D(samplerName, uv) which our macro expands to samplerName.Sample(samplerNameSampler, uv)
    int srv_extra_slot = 100; // Use high slots to avoid collision with main textures
    for (const auto &samp : fx.samplers)
    {
        oss << "SamplerState " << samp.name << "Sampler : register(s" << sampler_slot++ << ");\n";
        // Declare sampler name as Texture2D alias - shaders use tex2D(samplerName, uv)
        oss << "Texture2D<float4> " << samp.name << " : register(t" << srv_extra_slot++ << ");\n";
    }
    
    oss << "\n";
    
    return oss.str();
}

// ─── Helper Functions ────────────────────────────────────────────────────────

static std::string generate_helper_functions()
{
    return 
        "// [MariusFX] ReShade compatibility layer\n"
        "\n"
        "// Screen dimensions (TODO: pass as constants)\n"
        "#define BUFFER_WIDTH 1920\n"
        "#define BUFFER_HEIGHT 1080\n"
        "#define BUFFER_RCP_WIDTH (1.0 / 1920.0)\n"
        "#define BUFFER_RCP_HEIGHT (1.0 / 1080.0)\n"
        "#define BUFFER_PIXEL_SIZE float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT)\n"
        "#define BUFFER_SCREEN_SIZE float2(BUFFER_WIDTH, BUFFER_HEIGHT)\n"
        "\n"
        "// ReShade compatibility functions\n"
        "// Note: ReShade::BackBuffer is replaced with _ReShadeBackBuffer in source\n"
        "\n"
        "// Legacy tex2D function (ReShade compatibility)\n"
        "#define tex2D(s, t) s.Sample(s##Sampler, t)\n"
        "#define tex2Dlod(s, t) s.SampleLevel(s##Sampler, (t).xy, (t).w)\n"
        "#define tex2Dfetch(s, t) s.Load(int3((t).xy, 0))\n"
        "\n"
        "// Gather functions (for AO shaders like MiAO)\n"
        "#define tex2DgatherR(s, t) s.GatherRed(s##Sampler, t)\n"
        "#define tex2DgatherG(s, t) s.GatherGreen(s##Sampler, t)\n"
        "#define tex2DgatherB(s, t) s.GatherBlue(s##Sampler, t)\n"
        "#define tex2DgatherA(s, t) s.GatherAlpha(s##Sampler, t)\n"
        "\n"
        "// ReShade intrinsics (namespace functions)\n"
        "namespace ReShade {\n"
        "    bool HasNativeNormals(float2 texcoord) { return false; }\n"
        "    float3 GetNativeNormal(float2 texcoord) { return float3(0, 0, 1); }\n"
        "    bool HasNativeDepth(float2 texcoord) { return true; }\n"
        "    float GetLinearizedDepth(float2 texcoord) { return 0.5; }\n"
        "    float GetAspectRatio() { return BUFFER_WIDTH / (float)BUFFER_HEIGHT; }\n"
        "    float2 GetResolution() { return float2(BUFFER_WIDTH, BUFFER_HEIGHT); }\n"
        "    float2 GetPixelSize() { return float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT); }\n"
        "    float GetFrameTime() { return 16.67; }\n"
        "    uint GetFrameCount() { return 0; }\n"
        "}\n"
        "#define GetLinearizedDepth(tc) ReShade::GetLinearizedDepth(tc)\n"
        "    uint GetFrameCount() { return 0; }      // TODO: Pass from runtime\n"
        "}\n"
        "\n"
        "// Common ReShade helper functions\n"
        "float3 RGBToHSL(float3 rgb) {\n"
        "    float maxVal = max(max(rgb.r, rgb.g), rgb.b);\n"
        "    float minVal = min(min(rgb.r, rgb.g), rgb.b);\n"
        "    float l = (maxVal + minVal) / 2.0;\n"
        "    float s = 0.0;\n"
        "    float h = 0.0;\n"
        "    if (maxVal != minVal) {\n"
        "        float d = maxVal - minVal;\n"
        "        s = l > 0.5 ? d / (2.0 - maxVal - minVal) : d / (maxVal + minVal);\n"
        "        if (maxVal == rgb.r) h = (rgb.g - rgb.b) / d + (rgb.g < rgb.b ? 6.0 : 0.0);\n"
        "        else if (maxVal == rgb.g) h = (rgb.b - rgb.r) / d + 2.0;\n"
        "        else h = (rgb.r - rgb.g) / d + 4.0;\n"
        "        h /= 6.0;\n"
        "    }\n"
        "    return float3(h, s, l);\n"
        "}\n"
        "\n"
        "float3 HSLToRGB(float3 hsl) {\n"
        "    float h = hsl.x, s = hsl.y, l = hsl.z;\n"
        "    float c = (1.0 - abs(2.0 * l - 1.0)) * s;\n"
        "    float x = c * (1.0 - abs(fmod(h * 6.0, 2.0) - 1.0));\n"
        "    float m = l - c / 2.0;\n"
        "    float3 rgb = float3(0, 0, 0);\n"
        "    if (h < 1.0/6.0) rgb = float3(c, x, 0);\n"
        "    else if (h < 2.0/6.0) rgb = float3(x, c, 0);\n"
        "    else if (h < 3.0/6.0) rgb = float3(0, c, x);\n"
        "    else if (h < 4.0/6.0) rgb = float3(0, x, c);\n"
        "    else if (h < 5.0/6.0) rgb = float3(x, 0, c);\n"
        "    else rgb = float3(c, 0, x);\n"
        "    return rgb + m;\n"
        "}\n"
        "\n"
        "float3 RGBToHSV(float3 rgb) {\n"
        "    float maxVal = max(max(rgb.r, rgb.g), rgb.b);\n"
        "    float minVal = min(min(rgb.r, rgb.g), rgb.b);\n"
        "    float delta = maxVal - minVal;\n"
        "    float h = 0.0, s = 0.0, v = maxVal;\n"
        "    if (delta > 0.0) {\n"
        "        s = delta / maxVal;\n"
        "        if (rgb.r == maxVal) h = (rgb.g - rgb.b) / delta;\n"
        "        else if (rgb.g == maxVal) h = 2.0 + (rgb.b - rgb.r) / delta;\n"
        "        else h = 4.0 + (rgb.r - rgb.g) / delta;\n"
        "        h /= 6.0;\n"
        "        if (h < 0.0) h += 1.0;\n"
        "    }\n"
        "    return float3(h, s, v);\n"
        "}\n"
        "\n"
        "float3 HSVToRGB(float3 hsv) {\n"
        "    float h = hsv.x * 6.0;\n"
        "    float s = hsv.y;\n"
        "    float v = hsv.z;\n"
        "    float c = v * s;\n"
        "    float x = c * (1.0 - abs(fmod(h, 2.0) - 1.0));\n"
        "    float m = v - c;\n"
        "    float3 rgb = float3(0, 0, 0);\n"
        "    if (h < 1.0) rgb = float3(c, x, 0);\n"
        "    else if (h < 2.0) rgb = float3(x, c, 0);\n"
        "    else if (h < 3.0) rgb = float3(0, c, x);\n"
        "    else if (h < 4.0) rgb = float3(0, x, c);\n"
        "    else if (h < 5.0) rgb = float3(x, 0, c);\n"
        "    else rgb = float3(c, 0, x);\n"
        "    return rgb + m;\n"
        "}\n"
        "\n"
        "float GetLinearizedDepth(float2 texcoord) {\n"
        "    // TODO: Sample actual depth buffer\n"
        "    return 0.5;\n"
        "}\n"
        "\n"
        "// Common macros\n"
        "#define BLENDING_COMBO(src, dst, mode) lerp(dst, src, 0.5)\n"
        "// GLSL to HLSL compatibility\n"
        "#define mix(a, b, t) lerp(a, b, t)\n"
        "#define fract(x) frac(x)\n"
        "#define BUFFER_ASPECT_RATIO (float(BUFFER_WIDTH) / float(BUFFER_HEIGHT))\n"
        "\n"
        "// Fix tex2D/tex2Dlod for textures without explicit sampler\n"
        "#define tex2Dfetch0(s, t) s.Load(int3((t).xy, 0))\n"
        "\n"
        "// Helper to auto-extend float3 to float4 with alpha=1\n"
        "float4 float4_auto(float3 rgb) { return float4(rgb, 1.0); }\n"
        "float4 float4_auto(float2 rg) { return float4(rg, 0.0, 1.0); }\n"
        "float4 float4_auto(float r) { return float4(r, r, r, 1.0); }\n"
        "float4 float4_auto(float4 rgba) { return rgba; }\n"
        "\n"
        "// Legacy helper\n"
        "float linearize_depth(float depth)\n"
        "{\n"
        "    const float near_plane = 0.1;\n"
        "    const float far_plane = 10000.0;\n"
        "    return near_plane * far_plane / (far_plane - depth * (far_plane - near_plane));\n"
        "}\n"
        "\n";
}

// ─── Main Transpiler ─────────────────────────────────────────────────────────

TranspiledShader transpile_pass(
    const ParsedFX &fx,
    size_t technique_idx,
    size_t pass_idx,
    const ShaderClassification &classification,
    const TranspilerOptions &options)
{
    TranspiledShader result;
    result.success = false;
    result.original_filename = fx.filename;
    result.thread_group_size_x = options.thread_group_size_x;
    result.thread_group_size_y = options.thread_group_size_y;
    
    // Validate indices
    if (technique_idx >= fx.techniques.size())
    {
        result.error_message = "Invalid technique index";
        return result;
    }
    
    const auto &tech = fx.techniques[technique_idx];
    result.original_technique = tech.name;
    
    if (pass_idx >= tech.passes.size())
    {
        result.error_message = "Invalid pass index";
        return result;
    }
    
    const auto &pass = tech.passes[pass_idx];
    result.original_pass_index = static_cast<int>(pass_idx);
    
    // Get pixel shader source
    if (pass.pixel_shader.empty())
    {
        result.error_message = "No pixel shader in pass";
        return result;
    }
    
    auto it = fx.shader_functions.find(pass.pixel_shader);
    if (it == fx.shader_functions.end())
    {
        result.error_message = "Pixel shader function not found: " + pass.pixel_shader;
        return result;
    }
    
    const std::string &ps_source = it->second;
    
    // Check transpilability
    if (!is_transpilable(ps_source))
    {
        result.error_message = "Shader uses features incompatible with compute shaders (ddx/ddy/SampleGrad)";
        return result;
    }
    
    // Parse signature
    ParsedSignature ps_sig = parse_ps_signature(ps_source);
    if (ps_sig.function_name.empty())
    {
        result.error_message = "Failed to parse pixel shader signature";
        return result;
    }
    
    // Generate compute shader
    std::ostringstream oss;
    
    if (options.add_debug_markers)
    {
        oss << "// [MariusFX Transpiler]\n";
        oss << "// Original: " << fx.filename << "\n";
        oss << "// Technique: " << tech.name << "\n";
        oss << "// Pass: " << pass_idx << "\n";
        oss << "// Type: " << shader_type_name(classification.type) << "\n";
        oss << "// Priority: " << classification.priority << "\n";
        oss << "\n";
    }
    
    // Generate resources FIRST so _ReShadeBackBuffer is declared before use
    oss << "// [MariusFX] Generated resources\n";
    oss << generate_cbuffer(fx);
    oss << generate_texture_declarations(fx);
    oss << generate_sampler_declarations(fx);
    oss << generate_helper_functions();
    oss << "\n";
    
    // Include full source to preserve structures, helper functions, etc.
    // Strip ReShade annotations and replace namespace references
    std::string cleaned_source = strip_reshade_annotations(fx.source);
    cleaned_source = replace_reshade_namespace_refs(cleaned_source);
    
    oss << "// [MariusFX] Original shader source (cleaned)\n";
    oss << cleaned_source << "\n\n";
    
    oss << "// [MariusFX] Compute shader entry point\n";
    oss << generate_cs_signature(ps_sig, options);
    oss << convert_ps_body_to_cs(ps_source, ps_sig, options);
    
    // Apply ReShade namespace replacement to FINAL generated code
    std::string final_source = oss.str();
    final_source = replace_reshade_namespace_refs(final_source);
    
    // Strip UTF-8 BOM if present (EF BB BF)
    if (final_source.size() >= 3 && 
        (unsigned char)final_source[0] == 0xEF &&
        (unsigned char)final_source[1] == 0xBB &&
        (unsigned char)final_source[2] == 0xBF)
    {
        final_source = final_source.substr(3);
    }
    
    // Remove non-ASCII characters that HLSL compiler rejects
    std::string ascii_source;
    ascii_source.reserve(final_source.size());
    for (char c : final_source)
    {
        // Keep only ASCII printable + whitespace (tab, newline, carriage return)
        if ((c >= 32 && c <= 126) || c == '\t' || c == '\n' || c == '\r')
        {
            ascii_source += c;
        }
        // Skip other characters silently
    }
    
    result.compute_shader_source = ascii_source;
    result.entry_point = "CS_" + ps_sig.function_name;
    result.bindings = generate_bindings(fx);
    result.success = true;
    
    return result;
}

std::vector<TranspiledShader> transpile_technique(
    const ParsedFX &fx,
    size_t technique_idx,
    const ShaderClassification &classification,
    const TranspilerOptions &options)
{
    std::vector<TranspiledShader> results;
    
    if (technique_idx >= fx.techniques.size())
        return results;
    
    const auto &tech = fx.techniques[technique_idx];
    
    // If shader requires pixel shader features (derivatives, etc.), compile as native PS
    if (classification.requires_pixel_shader)
    {
        for (size_t i = 0; i < tech.passes.size(); ++i)
        {
            TranspiledShader result;
            result.is_pixel_shader = true;
            result.original_filename = fx.filename;
            result.original_technique = tech.name;
            result.original_pass_index = static_cast<int>(i);
            
            const auto &pass = tech.passes[i];
            
            // Find the pixel shader function
            auto ps_it = fx.shader_functions.find(pass.pixel_shader);
            if (ps_it == fx.shader_functions.end())
            {
                result.success = false;
                result.error_message = "Pixel shader function '" + pass.pixel_shader + "' not found";
                results.push_back(result);
                continue;
            }
            
            // Build native pixel shader source
            std::ostringstream oss;
            
            if (options.add_debug_markers)
            {
                oss << "// [MariusFX Native PS]\n";
                oss << "// Original: " << fx.filename << "\n";
                oss << "// Technique: " << tech.name << "\n";
                oss << "// Pass: " << i << "\n\n";
            }
            
            // Add BUFFER_* macros (same as CS preamble)
            oss << "#define BUFFER_WIDTH 1920\n";
            oss << "#define BUFFER_HEIGHT 1080\n";
            oss << "#define BUFFER_RCP_WIDTH (1.0 / 1920.0)\n";
            oss << "#define BUFFER_RCP_HEIGHT (1.0 / 1080.0)\n";
            oss << "#define BUFFER_SCREEN_SIZE float2(1920, 1080)\n\n";
            
            // Add ReShade intrinsics
            oss << "}\n\n";
            oss << "#define GetLinearizedDepth(tc) ReShade::GetLinearizedDepth(tc)\n";
            
            // Include full source (cleaned)
            
            // Include full source (cleaned)
            std::string cleaned_source = strip_reshade_annotations(fx.source);
            cleaned_source = replace_reshade_namespace_refs(cleaned_source);
            
            // Strip UTF-8 BOM and non-ASCII
            if (cleaned_source.size() >= 3 && 
                (unsigned char)cleaned_source[0] == 0xEF &&
                (unsigned char)cleaned_source[1] == 0xBB &&
                (unsigned char)cleaned_source[2] == 0xBF)
            {
                cleaned_source = cleaned_source.substr(3);
            }
            
            std::string ascii_source;
            ascii_source.reserve(cleaned_source.size());
            for (char c : cleaned_source)
            {
                if ((c >= 32 && c <= 126) || c == '\t' || c == '\n' || c == '\r')
                    ascii_source += c;
            }
            
            oss << ascii_source;
            
            result.compute_shader_source = oss.str();
            result.entry_point = pass.pixel_shader;
            result.bindings = generate_bindings(fx);
            result.success = true;
            
            results.push_back(result);
        }
    }
    else
    {
        // Transpile to compute shader as usual
        for (size_t i = 0; i < tech.passes.size(); ++i)
        {
            results.push_back(transpile_pass(fx, technique_idx, i, classification, options));
        }
    }
    
    return results;
}

} // namespace mariusfx::transpiler
