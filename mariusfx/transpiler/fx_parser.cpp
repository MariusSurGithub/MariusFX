/**
 * MariusFX — FX Parser Implementation
 * 
 * Parses ReShade .fx files using regex-based pattern matching.
 * This is NOT a full HLSL parser — it's a pragmatic solution that works
 * with 95% of real-world ReShade shaders.
 */

#include "fx_parser.hpp"
#include <regex>
#include <sstream>
#include <algorithm>
#include <cctype>
#include "../../source/dll_log.hpp"

namespace mariusfx::transpiler {

// ─── Utility Functions ───────────────────────────────────────────────────────

static std::string trim(const std::string &str)
{
    auto start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

static std::string remove_comments(const std::string &source)
{
    std::string result;
    result.reserve(source.size());
    
    bool in_line_comment = false;
    bool in_block_comment = false;
    bool in_string = false;
    
    for (size_t i = 0; i < source.size(); ++i)
    {
        char c = source[i];
        char next = (i + 1 < source.size()) ? source[i + 1] : '\0';
        
        if (in_string)
        {
            result += c;
            if (c == '"' && (i == 0 || source[i-1] != '\\'))
                in_string = false;
            continue;
        }
        
        if (in_line_comment)
        {
            if (c == '\n')
            {
                in_line_comment = false;
                result += c;  // Keep newlines for line numbers
            }
            continue;
        }
        
        if (in_block_comment)
        {
            if (c == '*' && next == '/')
            {
                in_block_comment = false;
                ++i;  // Skip '/'
            }
            else if (c == '\n')
            {
                result += c;  // Keep newlines
            }
            continue;
        }
        
        // Check for comment start
        if (c == '/' && next == '/')
        {
            in_line_comment = true;
            ++i;
            continue;
        }
        
        if (c == '/' && next == '*')
        {
            in_block_comment = true;
            ++i;
            continue;
        }
        
        if (c == '"')
            in_string = true;
        
        result += c;
    }
    
    return result;
}

// Simple preprocessor: expand #include "file.fxh"
static std::string expand_includes(const std::string &source, const std::string &base_path)
{
    std::regex include_regex("#include\\s+\"([^\"]+)\"");
    std::string result = source;
    std::smatch match;
    
    // For now, just remove #include directives (ReShade handles them)
    // TODO: Actually read and expand includes if needed
    result = std::regex_replace(result, include_regex, "// $&");
    
    return result;
}

// ─── Uniform Parsing ─────────────────────────────────────────────────────────

static std::vector<Uniform> parse_uniforms(const std::string &source)
{
    std::vector<Uniform> uniforms;
    
    // Pattern: uniform TYPE NAME < annotations > = DEFAULT;
    std::regex uniform_regex(
        "uniform\\s+(\\w+(?:\\d+)?)\\s+(\\w+)\\s*(?:<([^>]*)>)?\\s*(?:=\\s*([^;]+))?\\s*;"
    );
    
    auto begin = std::sregex_iterator(source.begin(), source.end(), uniform_regex);
    auto end = std::sregex_iterator();
    
    for (auto it = begin; it != end; ++it)
    {
        Uniform u;
        u.type = (*it)[1].str();
        u.name = (*it)[2].str();
        
        std::string annotations = (*it)[3].str();
        std::string default_val = (*it)[4].str();
        u.default_value = trim(default_val);
        
        // Parse annotations
        if (!annotations.empty())
        {
            std::regex ui_label_regex("ui_label\\s*=\\s*\"([^\"]*)\"");
            std::regex ui_tooltip_regex("ui_tooltip\\s*=\\s*\"([^\"]*)\"");
            std::regex ui_type_regex("ui_type\\s*=\\s*\"([^\"]*)\"");
            std::regex ui_min_regex("ui_min\\s*=\\s*([\\d.eE+-]+)");
            std::regex ui_max_regex("ui_max\\s*=\\s*([\\d.eE+-]+)");
            std::regex ui_step_regex("ui_step\\s*=\\s*([\\d.eE+-]+)");
            
            std::smatch m;
            if (std::regex_search(annotations, m, ui_label_regex))
                u.ui_label = m[1].str();
            if (std::regex_search(annotations, m, ui_tooltip_regex))
                u.ui_tooltip = m[1].str();
            if (std::regex_search(annotations, m, ui_type_regex))
                u.ui_type = m[1].str();
            if (std::regex_search(annotations, m, ui_min_regex))
                u.ui_min = std::stof(m[1].str());
            if (std::regex_search(annotations, m, ui_max_regex))
                u.ui_max = std::stof(m[1].str());
            if (std::regex_search(annotations, m, ui_step_regex))
                u.ui_step = std::stof(m[1].str());
        }
        
        uniforms.push_back(u);
    }
    
    return uniforms;
}

// ─── Texture Parsing ─────────────────────────────────────────────────────────

static std::vector<Texture> parse_textures(const std::string &source)
{
    std::vector<Texture> textures;
    
    // Pattern: texture NAME { Width = X; Height = Y; Format = FMT; };
    std::regex texture_regex(
        "texture\\s+(\\w+)\\s*\\{([^}]*)\\}"
    );
    
    auto begin = std::sregex_iterator(source.begin(), source.end(), texture_regex);
    auto end = std::sregex_iterator();
    
    for (auto it = begin; it != end; ++it)
    {
        Texture tex;
        tex.name = (*it)[1].str();
        
        std::string body = (*it)[2].str();
        
        // Parse properties
        std::regex width_regex("Width\\s*=\\s*(\\w+)");
        std::regex height_regex("Height\\s*=\\s*(\\w+)");
        std::regex format_regex("Format\\s*=\\s*(\\w+)");
        std::regex miplevels_regex("MipLevels\\s*=\\s*(\\d+)");
        
        std::smatch m;
        if (std::regex_search(body, m, width_regex))
        {
            std::string w = m[1].str();
            if (w == "BUFFER_WIDTH") tex.width = 0;
            else tex.width = std::stoi(w);
        }
        if (std::regex_search(body, m, height_regex))
        {
            std::string h = m[1].str();
            if (h == "BUFFER_HEIGHT") tex.height = 0;
            else tex.height = std::stoi(h);
        }
        if (std::regex_search(body, m, format_regex))
            tex.format = m[1].str();
        if (std::regex_search(body, m, miplevels_regex))
            tex.mip_levels = std::stoi(m[1].str());
        
        textures.push_back(tex);
    }
    
    return textures;
}

// ─── Sampler Parsing ─────────────────────────────────────────────────────────

static std::vector<Sampler> parse_samplers(const std::string &source)
{
    std::vector<Sampler> samplers;
    
    // Pattern: sampler NAME { Texture = TEX; AddressU = MODE; Filter = FILTER; };
    std::regex sampler_regex(
        "sampler\\s+(\\w+)\\s*\\{([^}]*)\\}"
    );
    
    auto begin = std::sregex_iterator(source.begin(), source.end(), sampler_regex);
    auto end = std::sregex_iterator();
    
    for (auto it = begin; it != end; ++it)
    {
        Sampler samp;
        samp.name = (*it)[1].str();
        
        std::string body = (*it)[2].str();
        
        std::regex texture_regex("Texture\\s*=\\s*(\\w+)");
        std::regex addressu_regex("AddressU\\s*=\\s*(\\w+)");
        std::regex addressv_regex("AddressV\\s*=\\s*(\\w+)");
        std::regex filter_regex("Filter\\s*=\\s*(\\w+)");
        
        std::smatch m;
        if (std::regex_search(body, m, texture_regex))
            samp.texture = m[1].str();
        if (std::regex_search(body, m, addressu_regex))
            samp.address_u = m[1].str();
        if (std::regex_search(body, m, addressv_regex))
            samp.address_v = m[1].str();
        if (std::regex_search(body, m, filter_regex))
            samp.filter = m[1].str();
        
        samplers.push_back(samp);
    }
    
    return samplers;
}

// ─── Technique Parsing ───────────────────────────────────────────────────────

static std::vector<Technique> parse_techniques(const std::string &source)
{
    std::vector<Technique> techniques;
    
    // Pattern: technique NAME < ... > { pass { ... } pass { ... } }
    // or: technique NAME { pass { ... } }
    // Handle optional UI annotations between name and opening brace
    std::regex technique_regex(
        "technique\\s+(\\w+)\\s*(?:<[^>]*>)?\\s*\\{([^}]*(?:\\{[^}]*\\}[^}]*)*)\\}"
    );
    
    auto begin = std::sregex_iterator(source.begin(), source.end(), technique_regex);
    auto end = std::sregex_iterator();
    
    for (auto it = begin; it != end; ++it)
    {
        Technique tech;
        tech.name = (*it)[1].str();
        
        std::string body = (*it)[2].str();
        
        // Parse passes
        std::regex pass_regex("pass\\s*(?:\\w+)?\\s*\\{([^}]*)\\}");
        auto pass_begin = std::sregex_iterator(body.begin(), body.end(), pass_regex);
        auto pass_end = std::sregex_iterator();
        
        for (auto pass_it = pass_begin; pass_it != pass_end; ++pass_it)
        {
            Pass pass;
            std::string pass_body = (*pass_it)[1].str();
            
            std::regex vs_regex("VertexShader\\s*=\\s*(\\w+)");
            std::regex ps_regex("PixelShader\\s*=\\s*(\\w+)");
            
            std::smatch m;
            if (std::regex_search(pass_body, m, vs_regex))
                pass.vertex_shader = m[1].str();
            if (std::regex_search(pass_body, m, ps_regex))
                pass.pixel_shader = m[1].str();
            
            tech.passes.push_back(pass);
        }
        
        techniques.push_back(tech);
    }
    
    return techniques;
}

// ─── Shader Function Extraction ─────────────────────────────────────────────

std::string extract_shader_function(const std::string &source, const std::string &entry)
{
    // Find function signature: TYPE ENTRY(...) : SEMANTIC { ... }
    std::string pattern = "\\w+\\s+" + entry + "\\s*\\([^)]*\\)\\s*(?::\\s*\\w+)?\\s*\\{";
    std::regex func_regex(pattern);
    
    std::smatch match;
    if (!std::regex_search(source, match, func_regex))
        return "";
    
    size_t start = match.position();
    size_t brace_start = source.find('{', start);
    if (brace_start == std::string::npos)
        return "";
    
    // Find matching closing brace
    int depth = 1;
    size_t pos = brace_start + 1;
    while (pos < source.size() && depth > 0)
    {
        if (source[pos] == '{') ++depth;
        else if (source[pos] == '}') --depth;
        ++pos;
    }
    
    if (depth != 0)
        return "";  // Unmatched braces
    
    return source.substr(start, pos - start);
}

// ─── Main Parser ─────────────────────────────────────────────────────────────

ParsedFX parse_fx(const std::string &fx_source, const std::string &filename)
{
    reshade::log::message(reshade::log::level::info, "[Parser] Step 1: Init");
    
    ParsedFX result;
    result.filename = filename;
    
    reshade::log::message(reshade::log::level::info, "[Parser] Step 2: Remove comments");
    // Preprocess
    std::string processed = remove_comments(fx_source);
    
    reshade::log::message(reshade::log::level::info, "[Parser] Step 3: Expand includes");
    processed = expand_includes(processed, filename);
    
    // Store processed source (with includes expanded and comments removed)
    result.source = processed;
    
    reshade::log::message(reshade::log::level::info, "[Parser] Step 4: Parse uniforms");
    // Parse components
    result.uniforms = parse_uniforms(processed);
    
    reshade::log::message(reshade::log::level::info, "[Parser] Step 5: Parse textures");
    result.textures = parse_textures(processed);
    
    reshade::log::message(reshade::log::level::info, "[Parser] Step 6: Parse samplers");
    result.samplers = parse_samplers(processed);
    
    reshade::log::message(reshade::log::level::info, "[Parser] Step 7: Parse techniques");
    result.techniques = parse_techniques(processed);
    
    reshade::log::message(reshade::log::level::info, "[Parser] Step 8: Extract shader functions");
    // Extract shader functions
    for (const auto &tech : result.techniques)
    {
        for (const auto &pass : tech.passes)
        {
            if (!pass.pixel_shader.empty())
            {
                std::string func = extract_shader_function(processed, pass.pixel_shader);
                if (!func.empty())
                    result.shader_functions[pass.pixel_shader] = func;
            }
            if (!pass.vertex_shader.empty())
            {
                std::string func = extract_shader_function(processed, pass.vertex_shader);
                if (!func.empty())
                    result.shader_functions[pass.vertex_shader] = func;
            }
        }
    }
    
    reshade::log::message(reshade::log::level::info, "[Parser] Step 9: Done");
    return result;
}

} // namespace mariusfx::transpiler
