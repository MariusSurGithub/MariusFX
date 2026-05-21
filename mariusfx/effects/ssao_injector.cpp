/**
 * MariusFX — SSAO Injector (Implementation)
 * 
 * Compiles and dispatches the SSAO compute shader.
 * 
 * Author: MariusFX Team
 * License: MIT
 */

#include "ssao_injector.hpp"
#include "../../source/dll_log.hpp"
#include <d3dcompiler.h>
#include <atomic>
#include <mutex>
#include <fstream>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

namespace mariusfx::ssao_injector {

// ─── Internal State ──────────────────────────────────────────────────────────

// ENABLED by default for testing. GBuffer capture is validated (RAGEGBufferDebug
// shows correct normals), so we can now test the SSAO injection in-game.
// User reported: post-process SSAO (MXAO/PPFXSSDO) bleeds through particles/smoke.
// Our injection runs BEFORE particles → should fix this.
static std::atomic<bool> g_enabled{true};
static std::atomic<bool> g_initialized{false};

static ID3D11Device *g_device = nullptr;
static ID3D11ComputeShader *g_compute_shader = nullptr;
static ID3D11Buffer *g_constant_buffer = nullptr;

static SSAOParams g_params = {
    0.5f,   // sample_radius
    1.5f,   // intensity
    8,      // sample_count
    0.9f,   // depth_fade_start
    0.99f,  // depth_fade_end
    0.1f    // bias_angle
};

static std::mutex g_params_mutex;

// ─── Constant Buffer Layout (must match HLSL cbuffer) ────────────────────────

struct CBParams
{
    float inv_resolution[2];
    float sample_radius;
    float intensity;
    uint32_t sample_count;
    float depth_fade_start;
    float depth_fade_end;
    float bias_angle;
};

// ─── Shader Source (embedded) ────────────────────────────────────────────────

// We embed the HLSL source as a string to avoid external file dependencies.
// In production, you could also load from a file or compile offline.

static const char *kShaderSource = R"HLSL(
// SSAO Compute Shader — see ssao_inject.hlsl for full source
// (This is a placeholder — in the real implementation, we'd include the full shader)

Texture2D<float4> tGBufferNormal   : register(t0);
Texture2D<float>  tDepthBuffer     : register(t1);
RWTexture2D<float4> uHDRBuffer     : register(u0);

cbuffer Params : register(b0)
{
    float2 InvResolution;
    float  SampleRadius;
    float  Intensity;
    uint   SampleCount;
    float  DepthFadeStart;
    float  DepthFadeEnd;
    float  BiasAngle;
};

float3 DecodeOctahedral(float2 oct)
{
    oct = oct * 2.0 - 1.0;
    float3 n = float3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    float t = saturate(-n.z);
    n.xy += (n.xy >= 0.0) ? -t : t;
    return normalize(n);
}

float3 UVToViewPos(float2 uv, float depth)
{
    float z = depth * 1000.0;
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float aspect = 1920.0 / 1080.0;
    return float3(ndc.x * z * aspect, ndc.y * z, z);
}

float Hash(uint2 p)
{
    uint h = p.x * 374761393u + p.y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float(h) * (1.0 / 4294967296.0);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dims;
    uHDRBuffer.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y)
        return;
    
    float2 uv = (DTid.xy + 0.5) * InvResolution;
    
    float4 normalSample = tGBufferNormal[DTid.xy];
    float depth = tDepthBuffer[DTid.xy];
    
    if (dot(normalSample.rgb, normalSample.rgb) < 0.0001)
        return;
    
    float3 normal = DecodeOctahedral(normalSample.rg);
    normal.z = -normal.z;
    
    float3 viewPos = UVToViewPos(uv, depth);
    
    float depthFade = 1.0 - smoothstep(DepthFadeStart, DepthFadeEnd, depth);
    if (depthFade < 0.01)
        return;
    
    float ao = 0.0;
    float randomAngle = Hash(DTid.xy) * 6.28318530718;
    float cosRand = cos(randomAngle);
    float sinRand = sin(randomAngle);
    
    for (uint i = 0; i < SampleCount; ++i)
    {
        float angle = (float(i) / float(SampleCount)) * 6.28318530718 + randomAngle;
        float2 sampleDir = float2(cos(angle), sin(angle));
        
        float2 rotatedDir = float2(
            sampleDir.x * cosRand - sampleDir.y * sinRand,
            sampleDir.x * sinRand + sampleDir.y * cosRand
        );
        
        float2 sampleUV = uv + rotatedDir * SampleRadius * InvResolution;
        
        if (any(sampleUV < 0.0) || any(sampleUV > 1.0))
            continue;
        
        uint2 sampleCoord = uint2(sampleUV / InvResolution);
        float sampleDepth = tDepthBuffer[sampleCoord];
        
        float3 samplePos = UVToViewPos(sampleUV, sampleDepth);
        
        float3 diff = samplePos - viewPos;
        float dist = length(diff);
        
        if (dist < 0.001)
            continue;
        
        float3 sampleDir3D = diff / dist;
        
        float horizonAngle = dot(normal, sampleDir3D);
        horizonAngle = max(horizonAngle - BiasAngle, 0.0);
        
        float falloff = 1.0 - smoothstep(0.0, SampleRadius, dist);
        
        ao += horizonAngle * falloff;
    }
    
    ao /= float(SampleCount);
    ao = 1.0 - saturate(ao * Intensity);
    ao = lerp(1.0, ao, depthFade);
    
    float4 hdr = uHDRBuffer[DTid.xy];
    uHDRBuffer[DTid.xy] = hdr * ao;
}
)HLSL";

// ─── Initialization ──────────────────────────────────────────────────────────

bool initialize(ID3D11Device *device)
{
    if (g_initialized.load(std::memory_order_relaxed))
    {
        reshade::log::message(reshade::log::level::warning,
            "[MariusFX SSAO] Already initialized");
        return true;
    }

    if (!device)
    {
        reshade::log::message(reshade::log::level::error,
            "[MariusFX SSAO] Null device passed to initialize()");
        return false;
    }

    g_device = device;
    g_device->AddRef();

    // ── Compile compute shader ──────────────────────────────────────────────

    ID3DBlob *shader_blob = nullptr;
    ID3DBlob *error_blob = nullptr;

    HRESULT hr = D3DCompile(
        kShaderSource,
        strlen(kShaderSource),
        "ssao_inject.hlsl",
        nullptr,                    // defines
        nullptr,                    // includes
        "main",                     // entry point
        "cs_5_0",                   // target
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &shader_blob,
        &error_blob
    );

    if (FAILED(hr))
    {
        if (error_blob)
        {
            reshade::log::message(reshade::log::level::error,
                "[MariusFX SSAO] Shader compilation failed:\n%s",
                (const char *)error_blob->GetBufferPointer());
            error_blob->Release();
        }
        else
        {
            reshade::log::message(reshade::log::level::error,
                "[MariusFX SSAO] Shader compilation failed (HRESULT 0x%08X)", hr);
        }
        g_device->Release();
        g_device = nullptr;
        return false;
    }

    if (error_blob)
        error_blob->Release();

    hr = g_device->CreateComputeShader(
        shader_blob->GetBufferPointer(),
        shader_blob->GetBufferSize(),
        nullptr,
        &g_compute_shader
    );

    shader_blob->Release();

    if (FAILED(hr))
    {
        reshade::log::message(reshade::log::level::error,
            "[MariusFX SSAO] CreateComputeShader failed (HRESULT 0x%08X)", hr);
        g_device->Release();
        g_device = nullptr;
        return false;
    }

    // ── Create constant buffer ──────────────────────────────────────────────

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(CBParams);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = g_device->CreateBuffer(&cbd, nullptr, &g_constant_buffer);

    if (FAILED(hr))
    {
        reshade::log::message(reshade::log::level::error,
            "[MariusFX SSAO] CreateBuffer (constant buffer) failed (HRESULT 0x%08X)", hr);
        g_compute_shader->Release();
        g_compute_shader = nullptr;
        g_device->Release();
        g_device = nullptr;
        return false;
    }

    g_initialized.store(true, std::memory_order_relaxed);

    reshade::log::message(reshade::log::level::info,
        "[MariusFX SSAO] Initialized successfully (CS compiled, CB created)");

    return true;
}

void shutdown()
{
    if (!g_initialized.load(std::memory_order_relaxed))
        return;

    if (g_constant_buffer)
    {
        g_constant_buffer->Release();
        g_constant_buffer = nullptr;
    }

    if (g_compute_shader)
    {
        g_compute_shader->Release();
        g_compute_shader = nullptr;
    }

    if (g_device)
    {
        g_device->Release();
        g_device = nullptr;
    }

    g_initialized.store(false, std::memory_order_relaxed);

    reshade::log::message(reshade::log::level::info,
        "[MariusFX SSAO] Shutdown complete");
}

bool is_ready()
{
    return g_initialized.load(std::memory_order_relaxed) && g_compute_shader != nullptr;
}

// ─── Injection ───────────────────────────────────────────────────────────────

void inject_ssao_pass(
    ID3D11DeviceContext *ctx,
    ID3D11ShaderResourceView *gbuffer_normal_srv,
    ID3D11ShaderResourceView *depth_srv,
    ID3D11UnorderedAccessView *hdr_buffer_uav,
    uint32_t width,
    uint32_t height)
{
    if (!g_enabled.load(std::memory_order_relaxed))
        return;

    if (!is_ready())
    {
        reshade::log::message(reshade::log::level::warning,
            "[MariusFX SSAO] inject_ssao_pass called but injector not ready");
        return;
    }

    if (!ctx || !gbuffer_normal_srv || !depth_srv || !hdr_buffer_uav)
    {
        reshade::log::message(reshade::log::level::warning,
            "[MariusFX SSAO] inject_ssao_pass called with null parameters");
        return;
    }

    // ── Update constant buffer ──────────────────────────────────────────────

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->Map(g_constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);

    if (FAILED(hr))
    {
        reshade::log::message(reshade::log::level::error,
            "[MariusFX SSAO] Failed to map constant buffer (HRESULT 0x%08X)", hr);
        return;
    }

    CBParams *cb = (CBParams *)mapped.pData;
    {
        std::lock_guard<std::mutex> lock(g_params_mutex);
        cb->inv_resolution[0] = 1.0f / float(width);
        cb->inv_resolution[1] = 1.0f / float(height);
        cb->sample_radius = g_params.sample_radius;
        cb->intensity = g_params.intensity;
        cb->sample_count = g_params.sample_count;
        cb->depth_fade_start = g_params.depth_fade_start;
        cb->depth_fade_end = g_params.depth_fade_end;
        cb->bias_angle = g_params.bias_angle;
    }

    ctx->Unmap(g_constant_buffer, 0);

    // ── Bind resources ──────────────────────────────────────────────────────

    ID3D11ShaderResourceView *srvs[] = { gbuffer_normal_srv, depth_srv };
    ctx->CSSetShaderResources(0, 2, srvs);

    ctx->CSSetUnorderedAccessViews(0, 1, &hdr_buffer_uav, nullptr);

    ctx->CSSetConstantBuffers(0, 1, &g_constant_buffer);

    ctx->CSSetShader(g_compute_shader, nullptr, 0);

    // ── Dispatch ────────────────────────────────────────────────────────────

    uint32_t dispatch_x = (width + 7) / 8;
    uint32_t dispatch_y = (height + 7) / 8;

    ctx->Dispatch(dispatch_x, dispatch_y, 1);

    // ── Unbind resources ────────────────────────────────────────────────────

    ID3D11ShaderResourceView *null_srvs[] = { nullptr, nullptr };
    ctx->CSSetShaderResources(0, 2, null_srvs);

    ID3D11UnorderedAccessView *null_uav = nullptr;
    ctx->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);

    ctx->CSSetShader(nullptr, nullptr, 0);

    // Log success (only once per session to avoid spam)
    static bool logged_once = false;
    if (!logged_once)
    {
        reshade::log::message(reshade::log::level::info,
            "[MariusFX SSAO] First injection successful (%ux%u, %u dispatches)",
            width, height, dispatch_x * dispatch_y);
        logged_once = true;
    }
}

// ─── Configuration ───────────────────────────────────────────────────────────

SSAOParams& get_params()
{
    return g_params;
}

void set_params(const SSAOParams &params)
{
    std::lock_guard<std::mutex> lock(g_params_mutex);
    g_params = params;
}

void set_enabled(bool enabled)
{
    g_enabled.store(enabled, std::memory_order_relaxed);
    reshade::log::message(reshade::log::level::info,
        "[MariusFX SSAO] %s", enabled ? "Enabled" : "Disabled");
}

bool is_enabled()
{
    return g_enabled.load(std::memory_order_relaxed);
}

} // namespace mariusfx::ssao_injector
