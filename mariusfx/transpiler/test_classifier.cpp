/**
 * MariusFX — Shader Classifier Test
 * 
 * Test the classifier on real ReShade shaders.
 */

#include "fx_parser.hpp"
#include "shader_classifier.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>

using namespace mariusfx::transpiler;
namespace fs = std::filesystem;

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
        std::cerr << "Usage: test_classifier <shader_directory>\n";
        std::cerr << "Example: test_classifier \"C:\\...\\reshade-shaders\\Shaders\"\n";
        return 1;
    }
    
    std::string shader_dir = argv[1];
    std::vector<std::string> fx_files;
    
    // Find all .fx files
    try
    {
        for (const auto &entry : fs::recursive_directory_iterator(shader_dir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".fx")
            {
                fx_files.push_back(entry.path().string());
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error scanning directory: " << e.what() << "\n";
        return 1;
    }
    
    std::cout << "Found " << fx_files.size() << " .fx files\n\n";
    
    // Classify each shader
    struct Result {
        std::string filename;
        ShaderType type;
        int priority;
        float confidence;
        std::string reason;
    };
    std::vector<Result> results;
    
    for (const auto &fx_path : fx_files)
    {
        std::string source = read_file(fx_path);
        if (source.empty())
            continue;
        
        ParsedFX fx = parse_fx(source, fx_path);
        ShaderClassification classification = classify_shader(fx);
        
        results.push_back({
            fs::path(fx_path).filename().string(),
            classification.type,
            classification.priority,
            classification.confidence,
            classification.reason
        });
    }
    
    // Sort by priority (ascending)
    std::sort(results.begin(), results.end(),
        [](const Result &a, const Result &b) { return a.priority < b.priority; });
    
    // Print results
    std::cout << "=== CLASSIFICATION RESULTS (sorted by priority) ===\n\n";
    std::cout << "Priority | Type                     | Conf  | Shader\n";
    std::cout << "---------|--------------------------|-------|----------------------------------\n";
    
    for (const auto &r : results)
    {
        printf("%4d     | %-24s | %.2f  | %s\n",
            r.priority,
            shader_type_name(r.type),
            r.confidence,
            r.filename.c_str());
    }
    
    // Statistics
    std::cout << "\n=== STATISTICS ===\n";
    int type_counts[15] = {};
    for (const auto &r : results)
        type_counts[static_cast<int>(r.type)]++;
    
    for (int i = 0; i < 15; ++i)
    {
        if (type_counts[i] > 0)
        {
            ShaderType type = static_cast<ShaderType>(i);
            printf("  %-24s : %d shaders\n", shader_type_name(type), type_counts[i]);
        }
    }
    
    // Low confidence warnings
    std::cout << "\n=== LOW CONFIDENCE WARNINGS ===\n";
    int low_conf_count = 0;
    for (const auto &r : results)
    {
        if (r.confidence < 0.7f)
        {
            printf("  %s (%.2f) - %s\n",
                r.filename.c_str(),
                r.confidence,
                shader_type_name(r.type));
            ++low_conf_count;
        }
    }
    if (low_conf_count == 0)
        std::cout << "  None! All shaders classified with high confidence.\n";
    
    std::cout << "\n✅ Classification complete!\n";
    return 0;
}
