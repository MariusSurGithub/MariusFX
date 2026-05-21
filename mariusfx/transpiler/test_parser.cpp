/**
 * MariusFX — FX Parser Test
 * 
 * Quick test to validate the parser on real ReShade shaders.
 */

#include "fx_parser.hpp"
#include <iostream>
#include <fstream>
#include <sstream>

using namespace mariusfx::transpiler;

static std::string read_file(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open())
        return "";
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: test_parser <path_to_fx_file>\n";
        return 1;
    }
    
    std::string fx_path = argv[1];
    std::string source = read_file(fx_path);
    
    if (source.empty())
    {
        std::cerr << "Failed to read file: " << fx_path << "\n";
        return 1;
    }
    
    std::cout << "Parsing: " << fx_path << "\n";
    std::cout << "Source size: " << source.size() << " bytes\n\n";
    
    ParsedFX fx = parse_fx(source, fx_path);
    
    // Print results
    std::cout << "=== UNIFORMS (" << fx.uniforms.size() << ") ===\n";
    for (const auto &u : fx.uniforms)
    {
        std::cout << "  " << u.type << " " << u.name;
        if (!u.default_value.empty())
            std::cout << " = " << u.default_value;
        if (!u.ui_label.empty())
            std::cout << "  // " << u.ui_label;
        std::cout << "\n";
    }
    
    std::cout << "\n=== TEXTURES (" << fx.textures.size() << ") ===\n";
    for (const auto &tex : fx.textures)
    {
        std::cout << "  " << tex.name << " : ";
        if (tex.width == 0) std::cout << "BUFFER_WIDTH";
        else std::cout << tex.width;
        std::cout << " x ";
        if (tex.height == 0) std::cout << "BUFFER_HEIGHT";
        else std::cout << tex.height;
        std::cout << ", " << tex.format << "\n";
    }
    
    std::cout << "\n=== SAMPLERS (" << fx.samplers.size() << ") ===\n";
    for (const auto &samp : fx.samplers)
    {
        std::cout << "  " << samp.name << " -> " << samp.texture;
        if (!samp.filter.empty())
            std::cout << " (" << samp.filter << ")";
        std::cout << "\n";
    }
    
    std::cout << "\n=== TECHNIQUES (" << fx.techniques.size() << ") ===\n";
    for (const auto &tech : fx.techniques)
    {
        std::cout << "  " << tech.name << " (" << tech.passes.size() << " passes)\n";
        for (size_t i = 0; i < tech.passes.size(); ++i)
        {
            const auto &pass = tech.passes[i];
            std::cout << "    Pass " << i << ": ";
            if (!pass.vertex_shader.empty())
                std::cout << "VS=" << pass.vertex_shader << " ";
            if (!pass.pixel_shader.empty())
                std::cout << "PS=" << pass.pixel_shader;
            std::cout << "\n";
        }
    }
    
    std::cout << "\n=== SHADER FUNCTIONS (" << fx.shader_functions.size() << ") ===\n";
    for (const auto &[name, body] : fx.shader_functions)
    {
        std::cout << "  " << name << " (" << body.size() << " chars)\n";
        // Print first 100 chars
        std::string preview = body.substr(0, std::min<size_t>(100, body.size()));
        std::replace(preview.begin(), preview.end(), '\n', ' ');
        std::cout << "    " << preview << "...\n";
    }
    
    std::cout << "\n✅ Parsing complete!\n";
    return 0;
}
