/**
 * MariusFX — HLSL Transpiler Test
 * 
 * Test the transpiler on a real ReShade shader.
 */

#include "fx_parser.hpp"
#include "shader_classifier.hpp"
#include "hlsl_transpiler.hpp"
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

static void write_file(const std::string &path, const std::string &content)
{
    std::ofstream file(path);
    file << content;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: test_transpiler <path_to_fx_file> [output_hlsl_file]\n";
        std::cerr << "Example: test_transpiler MXAO.fx MXAO_transpiled.hlsl\n";
        return 1;
    }
    
    std::string fx_path = argv[1];
    std::string output_path = (argc >= 3) ? argv[2] : "transpiled_output.hlsl";
    
    std::string source = read_file(fx_path);
    if (source.empty())
    {
        std::cerr << "Failed to read file: " << fx_path << "\n";
        return 1;
    }
    
    std::cout << "=== TRANSPILING: " << fx_path << " ===\n\n";
    
    // Step 1: Parse
    std::cout << "[1/4] Parsing .fx file...\n";
    ParsedFX fx = parse_fx(source, fx_path);
    std::cout << "  Found " << fx.techniques.size() << " techniques\n";
    std::cout << "  Found " << fx.uniforms.size() << " uniforms\n";
    std::cout << "  Found " << fx.textures.size() << " textures\n\n";
    
    // Step 2: Classify
    std::cout << "[2/4] Classifying shader...\n";
    ShaderClassification classification = classify_shader(fx);
    std::cout << "  Type: " << shader_type_name(classification.type) << "\n";
    std::cout << "  Priority: " << classification.priority << "\n";
    std::cout << "  Confidence: " << classification.confidence << "\n";
    std::cout << "  Reason: " << classification.reason << "\n";
    std::cout << "  Needs depth: " << (classification.needs_depth ? "YES" : "NO") << "\n";
    std::cout << "  Is HDR: " << (classification.is_hdr ? "YES" : "NO") << "\n\n";
    
    // Step 3: Transpile
    std::cout << "[3/4] Transpiling to compute shader...\n";
    
    if (fx.techniques.empty())
    {
        std::cerr << "ERROR: No techniques found!\n";
        return 1;
    }
    
    TranspilerOptions opts;
    opts.add_debug_markers = true;
    opts.thread_group_size_x = 8;
    opts.thread_group_size_y = 8;
    
    auto transpiled = transpile_technique(fx, 0, classification, opts);
    
    if (transpiled.empty())
    {
        std::cerr << "ERROR: Transpilation failed (no passes)!\n";
        return 1;
    }
    
    std::cout << "  Transpiled " << transpiled.size() << " passes\n\n";
    
    // Step 4: Output
    std::cout << "[4/4] Writing output...\n";
    
    std::ostringstream full_output;
    
    for (size_t i = 0; i < transpiled.size(); ++i)
    {
        const auto &pass = transpiled[i];
        
        if (!pass.success)
        {
            std::cerr << "  Pass " << i << " FAILED: " << pass.error_message << "\n";
            continue;
        }
        
        std::cout << "  Pass " << i << " OK: " << pass.entry_point << " (" << pass.compute_shader_source.size() << " bytes)\n";
        
        full_output << "// ============================================================\n";
        full_output << "// PASS " << i << " : " << pass.entry_point << "\n";
        full_output << "// ============================================================\n\n";
        full_output << pass.compute_shader_source << "\n\n";
    }
    
    write_file(output_path, full_output.str());
    std::cout << "\n✅ Transpilation complete!\n";
    std::cout << "Output written to: " << output_path << "\n";
    
    // Print first 50 lines of output for preview
    std::cout << "\n=== OUTPUT PREVIEW (first 50 lines) ===\n";
    std::istringstream iss(full_output.str());
    std::string line;
    int line_count = 0;
    while (std::getline(iss, line) && line_count++ < 50)
    {
        std::cout << line << "\n";
    }
    if (line_count >= 50)
        std::cout << "... (truncated)\n";
    
    return 0;
}
