/**
 * MariusFX — Pipeline Scheduler Implementation
 * 
 * Manages execution of transpiled shaders at the correct pipeline stages.
 */

#include "pipeline_scheduler.hpp"
#include <d3dcompiler.h>
#include <algorithm>
#include <sstream>
#include <fstream>
#include "../../source/dll_log.hpp"

#pragma comment(lib, "d3dcompiler.lib")

namespace mariusfx::transpiler {

// ─── Global Scheduler Instance ───────────────────────────────────────────────

PipelineScheduler& get_scheduler()
{
    static PipelineScheduler g_scheduler;
    return g_scheduler;
}

// ─── Helper Functions ────────────────────────────────────────────────────────

InjectionPoint PipelineScheduler::priority_to_injection_point(int priority)
{
    if (priority < PRIORITY_BEFORE_LIGHTING)
        return InjectionPoint::AFTER_GBUFFER;
    else if (priority < PRIORITY_TONEMAP)
        return InjectionPoint::BEFORE_TONEMAP;
    else if (priority < PRIORITY_ANTI_ALIASING)
        return InjectionPoint::AFTER_TONEMAP;
    else if (priority < PRIORITY_COSMETIC)
        return InjectionPoint::AFTER_AA;
    else
        return InjectionPoint::BEFORE_UI;
}

// ─── Shader Compilation ──────────────────────────────────────────────────────

static bool compile_pixel_shader(
    ID3D11Device *device,
    const std::string &source,
    const std::string &entry_point,
    ID3D11PixelShader **out_shader,
    std::string &error_message)
{
    ID3DBlob *shader_blob = nullptr;
    ID3DBlob *error_blob = nullptr;
    
    HRESULT hr = D3DCompile(
        source.c_str(),
        source.size(),
        nullptr,
        nullptr,
        nullptr,
        entry_point.c_str(),
        "ps_5_0",  // Pixel Shader 5.0
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &shader_blob,
        &error_blob
    );
    
    if (FAILED(hr))
    {
        if (error_blob)
        {
            error_message = std::string(
                static_cast<const char*>(error_blob->GetBufferPointer()),
                error_blob->GetBufferSize()
            );
            error_blob->Release();
        }
        else
        {
            error_message = "D3DCompile failed with HRESULT " + std::to_string(hr);
        }
        
        // DEBUG: Dump failing shaders for inspection (limited count)
        static int fail_dump_counter = 0;
        if (fail_dump_counter < 100)
        {
            std::string dump_path = "C:\\Users\\Marius\\AppData\\Local\\FiveM\\FiveM.app\\plugins\\fail_dump_ps_" + std::to_string(fail_dump_counter++) + ".hlsl";
            std::ofstream dump_file(dump_path);
            if (dump_file.is_open())
            {
                dump_file << "// PIXEL SHADER COMPILATION ERROR:\n// " << error_message << "\n\n";
                dump_file << source;
                dump_file.close();
            }
        }
        
        if (shader_blob)
            shader_blob->Release();
        
        return false;
    }
    
    hr = device->CreatePixelShader(
        shader_blob->GetBufferPointer(),
        shader_blob->GetBufferSize(),
        nullptr,
        out_shader
    );
    
    shader_blob->Release();
    
    if (FAILED(hr))
    {
        error_message = "CreatePixelShader failed with HRESULT " + std::to_string(hr);
        return false;
    }
    
    return true;
}

static bool compile_compute_shader(
    ID3D11Device *device,
    const std::string &source,
    const std::string &entry_point,
    ID3D11ComputeShader **out_shader,
    std::string &error_message)
{
    ID3DBlob *shader_blob = nullptr;
    ID3DBlob *error_blob = nullptr;
    
    // TODO: Implement custom include handler for ReShade.fxh
    // For now, disable includes - the full source is already embedded
    HRESULT hr = D3DCompile(
        source.c_str(),
        source.size(),
        nullptr,                    // source name
        nullptr,                    // defines
        nullptr,                    // include handler (disabled for now)
        entry_point.c_str(),
        "cs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &shader_blob,
        &error_blob
    );
    
    if (FAILED(hr))
    {
        if (error_blob)
        {
            error_message = std::string(
                static_cast<const char*>(error_blob->GetBufferPointer()),
                error_blob->GetBufferSize()
            );
            error_blob->Release();
        }
        else
        {
            error_message = "D3DCompile failed with HRESULT " + std::to_string(hr);
        }
        
        // DEBUG: Dump failing shaders for inspection (limited count)
        static int fail_dump_counter = 0;
        if (fail_dump_counter < 100)  // Temporarily increased to catch PPFX_SSDO
        {
            std::string dump_path = "C:\\Users\\Marius\\AppData\\Local\\FiveM\\FiveM.app\\plugins\\fail_dump_" + std::to_string(fail_dump_counter++) + ".hlsl";
            std::ofstream dump_file(dump_path);
            if (dump_file.is_open())
            {
                dump_file << "// COMPILATION ERROR:\n// " << error_message << "\n\n";
                dump_file << source;
                dump_file.close();
            }
        }
        
        if (shader_blob)
            shader_blob->Release();
        
        return false;
    }
    
    hr = device->CreateComputeShader(
        shader_blob->GetBufferPointer(),
        shader_blob->GetBufferSize(),
        nullptr,
        out_shader
    );
    
    shader_blob->Release();
    
    if (FAILED(hr))
    {
        error_message = "CreateComputeShader failed with HRESULT " + std::to_string(hr);
        return false;
    }
    
    return true;
}

// ─── PipelineScheduler Implementation ───────────────────────────────────────

bool PipelineScheduler::initialize(ID3D11Device *device)
{
    if (!device)
        return false;
    
    reshade::log::message(reshade::log::level::info, "[Scheduler] initialize() called with device=%p", device);
    
    // Clear any existing state BEFORE setting new device
    if (m_device)
    {
        reshade::log::message(reshade::log::level::info, "[Scheduler] Clearing existing state");
        shutdown();
    }
    
    m_device = device;
    reshade::log::message(reshade::log::level::info, "[Scheduler] Device set to %p", m_device);
    
    return true;
}

void PipelineScheduler::shutdown()
{
    // Release all compiled resources
    for (auto &shader : m_shaders)
    {
        for (auto &pass : shader->compiled_passes)
        {
            if (pass.is_pixel_shader && pass.shader_ptr.pixel_shader)
            {
                pass.shader_ptr.pixel_shader->Release();
                pass.shader_ptr.pixel_shader = nullptr;
            }
            else if (!pass.is_pixel_shader && pass.shader_ptr.compute_shader)
            {
                pass.shader_ptr.compute_shader->Release();
                pass.shader_ptr.compute_shader = nullptr;
            }
            if (pass.cbuffer)
            {
                pass.cbuffer->Release();
                pass.cbuffer = nullptr;
            }
            for (auto *srv : pass.srvs)
                if (srv) srv->Release();
            for (auto *uav : pass.uavs)
                if (uav) uav->Release();
            for (auto *sampler : pass.samplers)
                if (sampler) sampler->Release();
            
            pass.srvs.clear();
            pass.uavs.clear();
            pass.samplers.clear();
        }
    }
    
    m_shaders.clear();
    
    for (int i = 0; i < 5; ++i)
        m_shaders_by_point[i].clear();
    
    m_device = nullptr;
}

bool PipelineScheduler::register_shader(
    const ParsedFX &fx,
    size_t technique_idx,
    const ShaderClassification &classification)
{
    reshade::log::message(reshade::log::level::info, "[Scheduler] register_shader: %s", fx.filename.c_str());
    
    if (!m_device)
    {
        reshade::log::message(reshade::log::level::error, "[Scheduler] Device is null!");
        return false;
    }
    
    reshade::log::message(reshade::log::level::info, "[Scheduler] Transpiling technique...");
    
    // Transpile the shader
    TranspilerOptions opts;
    opts.add_debug_markers = true;
    opts.thread_group_size_x = 8;
    opts.thread_group_size_y = 8;
    
    auto transpiled_passes = transpile_technique(fx, technique_idx, classification, opts);
    
    reshade::log::message(reshade::log::level::info, "[Scheduler] Transpiled %zu passes", transpiled_passes.size());
    
    if (transpiled_passes.empty())
    {
        reshade::log::message(reshade::log::level::error, "[Scheduler] No passes transpiled!");
        return false;
    }
    
    // Check if any pass failed
    for (const auto &pass : transpiled_passes)
    {
        if (!pass.success)
        {
            reshade::log::message(reshade::log::level::error, "[Scheduler] Pass failed: %s", pass.error_message.c_str());
            return false;
        }
    }
    
    reshade::log::message(reshade::log::level::info, "[Scheduler] Creating scheduled shader...");
    
    // Create scheduled shader
    auto scheduled = std::make_shared<ScheduledShader>();
    scheduled->name = fx.techniques[technique_idx].name;
    scheduled->original_fx_path = fx.filename;
    scheduled->type = classification.type;
    scheduled->priority = classification.priority;
    scheduled->injection_point = priority_to_injection_point(classification.priority);
    scheduled->passes = transpiled_passes;
    scheduled->enabled = true;
    scheduled->compiled = false;
    
    reshade::log::message(reshade::log::level::info, "[Scheduler] Compiling shader...");
    
    // Compile the shader
    if (!compile_shader(scheduled.get()))
    {
        reshade::log::message(reshade::log::level::error, "[Scheduler] Compilation failed: %s", scheduled->error_message.c_str());
        return false;
    }
    
    reshade::log::message(reshade::log::level::info, "[Scheduler] Shader registered successfully!");
    
    // Add to list
    m_shaders.push_back(scheduled);
    
    // Re-sort
    sort_shaders();
    
    return true;
}

bool PipelineScheduler::compile_shader(ScheduledShader *shader)
{
    if (!m_device || !shader)
        return false;
    
    shader->compiled_passes.clear();
    shader->compiled_passes.resize(shader->passes.size());
    
    for (size_t i = 0; i < shader->passes.size(); ++i)
    {
        const auto &transpiled = shader->passes[i];
        auto &compiled = shader->compiled_passes[i];
        
        std::string error;
        
        // Compile shader (PS or CS depending on type)
        if (transpiled.is_pixel_shader)
        {
            compiled.is_pixel_shader = true;
            if (!compile_pixel_shader(
                m_device,
                transpiled.compute_shader_source,
                transpiled.entry_point,
                &compiled.shader_ptr.pixel_shader,
                error))
            {
                shader->failed = true;
                shader->error_message = "Pass " + std::to_string(i) + " (PS): " + error;
                return false;
            }
        }
        else
        {
            compiled.is_pixel_shader = false;
            if (!compile_compute_shader(
                m_device,
                transpiled.compute_shader_source,
                transpiled.entry_point,
                &compiled.shader_ptr.compute_shader,
                error))
            {
                shader->failed = true;
                shader->error_message = "Pass " + std::to_string(i) + " (CS): " + error;
                return false;
            }
        }
        
        // Create constant buffer
        // TODO: Calculate actual size from uniforms
        D3D11_BUFFER_DESC cbuf_desc = {};
        cbuf_desc.ByteWidth = 256;  // Placeholder
        cbuf_desc.Usage = D3D11_USAGE_DYNAMIC;
        cbuf_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbuf_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        
        HRESULT hr = m_device->CreateBuffer(&cbuf_desc, nullptr, &compiled.cbuffer);
        if (FAILED(hr))
        {
            shader->failed = true;
            shader->error_message = "Failed to create constant buffer";
            return false;
        }
        
        // TODO: Create SRVs, UAVs, samplers
        // For now, we'll bind them at runtime from the GBuffer capture
    }
    
    shader->compiled = true;
    shader->failed = false;
    return true;
}

void PipelineScheduler::sort_shaders()
{
    // Sort by priority (ascending)
    std::sort(m_shaders.begin(), m_shaders.end(),
        [](const auto &a, const auto &b) { return a->priority < b->priority; });
    
    // Rebuild by-point lookup
    for (int i = 0; i < 5; ++i)
        m_shaders_by_point[i].clear();
    
    for (auto &shader : m_shaders)
    {
        int point_idx = static_cast<int>(shader->injection_point);
        m_shaders_by_point[point_idx].push_back(shader.get());
    }
}

void PipelineScheduler::execute_at_point(
    ID3D11DeviceContext *ctx,
    InjectionPoint point,
    uint32_t width,
    uint32_t height)
{
    if (!ctx)
        return;
    
    int point_idx = static_cast<int>(point);
    const auto &shaders = m_shaders_by_point[point_idx];
    
    for (auto *shader : shaders)
    {
        if (!shader->enabled || !shader->compiled || shader->failed)
            continue;
        
        // Execute all passes
        for (size_t i = 0; i < shader->compiled_passes.size(); ++i)
        {
            const auto &pass = shader->compiled_passes[i];
            
            // Skip if no shader compiled
            if (pass.is_pixel_shader && !pass.shader_ptr.pixel_shader)
                continue;
            if (!pass.is_pixel_shader && !pass.shader_ptr.compute_shader)
                continue;
            
            // Bind shader (only CS supported in dispatch for now, PS needs fullscreen quad)
            if (!pass.is_pixel_shader)
            {
                ctx->CSSetShader(pass.shader_ptr.compute_shader, nullptr, 0);
            }
            else
            {
                // TODO: Implement PS execution via fullscreen quad
                continue;
            }
            
            // Bind constant buffer
            if (pass.cbuffer)
            {
                // TODO: Update cbuffer with actual uniform values
                ctx->CSSetConstantBuffers(0, 1, &pass.cbuffer);
            }
            
            // TODO: Bind SRVs, UAVs, samplers
            // For now, assume they're already bound by the caller
            
            // Dispatch
            uint32_t groups_x = (width + 7) / 8;
            uint32_t groups_y = (height + 7) / 8;
            ctx->Dispatch(groups_x, groups_y, 1);
            
            // Unbind UAVs (important to avoid hazards)
            ID3D11UnorderedAccessView *null_uavs[8] = {};
            ctx->CSSetUnorderedAccessViews(0, 8, null_uavs, nullptr);
            
            shader->total_dispatches++;
        }
    }
}

void PipelineScheduler::set_shader_enabled(const std::string &name, bool enabled)
{
    for (auto &shader : m_shaders)
    {
        if (shader->name == name)
        {
            shader->enabled = enabled;
            return;
        }
    }
}

bool PipelineScheduler::reload_shader(const std::string &name)
{
    for (auto &shader : m_shaders)
    {
        if (shader->name == name)
        {
            // Release old resources
            for (auto &pass : shader->compiled_passes)
            {
                if (pass.is_pixel_shader && pass.shader_ptr.pixel_shader)
                {
                    pass.shader_ptr.pixel_shader->Release();
                    pass.shader_ptr.pixel_shader = nullptr;
                }
                else if (!pass.is_pixel_shader && pass.shader_ptr.compute_shader)
                {
                    pass.shader_ptr.compute_shader->Release();
                    pass.shader_ptr.compute_shader = nullptr;
                }
                if (pass.cbuffer)
                {
                    pass.cbuffer->Release();
                    pass.cbuffer = nullptr;
                }
            }
            
            // Recompile
            return compile_shader(shader.get());
        }
    }
    
    return false;
}

} // namespace mariusfx::transpiler
