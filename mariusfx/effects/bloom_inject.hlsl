/**
 * MariusFX — Bloom HDR Compute Shader (Pipeline Injection)
 * 
 * This compute shader is injected BEFORE the tonemap pass, while the image
 * is still in HDR (linear space). This produces photoréalistic bloom like ENB.
 * 
 * Technique: Dual Kawase Blur (fast, high-quality bloom)
 * 
 * Author: MariusFX Team
 * License: MIT
 */

// ─── Input/Output Textures ───────────────────────────────────────────────────

Texture2D<float4> tHDRBuffer       : register(t0); // HDR buffer (R16G16B16A16_FLOAT)
RWTexture2D<float4> uHDRBuffer     : register(u0); // HDR buffer (read-write)

// Temporary buffers for downscale/blur passes
RWTexture2D<float4> uBloomTemp1    : register(u1); // 1/2 resolution
RWTexture2D<float4> uBloomTemp2    : register(u2); // 1/4 resolution
RWTexture2D<float4> uBloomTemp3    : register(u3); // 1/8 resolution

// ─── Constant Buffer ─────────────────────────────────────────────────────────

cbuffer Params : register(b0)
{
    float2 Resolution;         // Full resolution (e.g., 1920x1080)
    float  Threshold;          // Luminance threshold (e.g., 1.0)
    float  Intensity;          // Bloom intensity (e.g., 0.5)
    float  Radius;             // Blur radius multiplier (e.g., 1.0)
    uint   PassIndex;          // Current pass (0=bright, 1=down1, 2=down2, etc.)
    float2 Padding;
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Luminance (Rec. 709)
float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Bright-pass filter (extract bright pixels above threshold)
float3 BrightPass(float3 color)
{
    float lum = Luminance(color);
    float bloom_amount = max(lum - Threshold, 0.0);
    return color * (bloom_amount / max(lum, 0.0001));
}

// Kawase blur sample pattern (4 samples in a + pattern)
float4 KawaseBlur(Texture2D<float4> tex, float2 uv, float2 texel_size, float offset)
{
    float4 sum = 0.0;
    
    // Sample in a diamond pattern
    sum += tex.SampleLevel(sampler_linear_clamp, uv + float2(+offset, +offset) * texel_size, 0);
    sum += tex.SampleLevel(sampler_linear_clamp, uv + float2(+offset, -offset) * texel_size, 0);
    sum += tex.SampleLevel(sampler_linear_clamp, uv + float2(-offset, +offset) * texel_size, 0);
    sum += tex.SampleLevel(sampler_linear_clamp, uv + float2(-offset, -offset) * texel_size, 0);
    
    return sum * 0.25;
}

// ─── Sampler ─────────────────────────────────────────────────────────────────

SamplerState sampler_linear_clamp : register(s0);

// ─── Pass 0: Bright-Pass Filter ──────────────────────────────────────────────

[numthreads(8, 8, 1)]
void BrightPassCS(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dims;
    uBloomTemp1.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y)
        return;
    
    // Sample HDR buffer at full resolution
    float2 uv = (DTid.xy + 0.5) / float2(dims);
    float4 hdr = tHDRBuffer.SampleLevel(sampler_linear_clamp, uv, 0);
    
    // Apply bright-pass filter
    float3 bright = BrightPass(hdr.rgb);
    
    // Write to 1/2 resolution temp buffer
    uBloomTemp1[DTid.xy] = float4(bright, 1.0);
}

// ─── Pass 1: Downscale 1/2 → 1/4 with Blur ───────────────────────────────────

[numthreads(8, 8, 1)]
void DownscaleBlur1CS(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dims;
    uBloomTemp2.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y)
        return;
    
    float2 uv = (DTid.xy + 0.5) / float2(dims);
    float2 texel_size = 1.0 / float2(dims);
    
    // Kawase blur from temp1 (1/2 res)
    float4 blurred = KawaseBlur(uBloomTemp1, uv, texel_size, 1.0 * Radius);
    
    uBloomTemp2[DTid.xy] = blurred;
}

// ─── Pass 2: Downscale 1/4 → 1/8 with Blur ───────────────────────────────────

[numthreads(8, 8, 1)]
void DownscaleBlur2CS(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dims;
    uBloomTemp3.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y)
        return;
    
    float2 uv = (DTid.xy + 0.5) / float2(dims);
    float2 texel_size = 1.0 / float2(dims);
    
    // Kawase blur from temp2 (1/4 res)
    float4 blurred = KawaseBlur(uBloomTemp2, uv, texel_size, 1.5 * Radius);
    
    uBloomTemp3[DTid.xy] = blurred;
}

// ─── Pass 3: Upscale 1/8 → 1/4 with Blur ─────────────────────────────────────

[numthreads(8, 8, 1)]
void UpscaleBlur1CS(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dims;
    uBloomTemp2.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y)
        return;
    
    float2 uv = (DTid.xy + 0.5) / float2(dims);
    float2 texel_size = 1.0 / float2(dims * 0.5); // Sample from 1/8 res
    
    // Kawase blur from temp3 (1/8 res)
    float4 blurred = KawaseBlur(uBloomTemp3, uv, texel_size, 1.5 * Radius);
    
    // Add to existing temp2 content (accumulate)
    uBloomTemp2[DTid.xy] += blurred;
}

// ─── Pass 4: Upscale 1/4 → 1/2 with Blur ─────────────────────────────────────

[numthreads(8, 8, 1)]
void UpscaleBlur2CS(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dims;
    uBloomTemp1.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y)
        return;
    
    float2 uv = (DTid.xy + 0.5) / float2(dims);
    float2 texel_size = 1.0 / float2(dims * 0.5); // Sample from 1/4 res
    
    // Kawase blur from temp2 (1/4 res)
    float4 blurred = KawaseBlur(uBloomTemp2, uv, texel_size, 1.0 * Radius);
    
    // Add to existing temp1 content (accumulate)
    uBloomTemp1[DTid.xy] += blurred;
}

// ─── Pass 5: Composite Bloom onto HDR Buffer ─────────────────────────────────

[numthreads(8, 8, 1)]
void CompositeCS(uint3 DTid : SV_DispatchThreadID)
{
    uint2 dims;
    uHDRBuffer.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y)
        return;
    
    float2 uv = (DTid.xy + 0.5) / float2(dims);
    
    // Sample bloom from 1/2 res temp buffer
    float4 bloom = uBloomTemp1.SampleLevel(sampler_linear_clamp, uv, 0);
    
    // Read current HDR value
    float4 hdr = uHDRBuffer[DTid.xy];
    
    // Add bloom (additive blending in HDR)
    hdr.rgb += bloom.rgb * Intensity;
    
    // Write back
    uHDRBuffer[DTid.xy] = hdr;
}

// ─── Entry Points (selected via PassIndex) ───────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Dispatch the appropriate pass based on PassIndex
    switch (PassIndex)
    {
    case 0: BrightPassCS(DTid); break;
    case 1: DownscaleBlur1CS(DTid); break;
    case 2: DownscaleBlur2CS(DTid); break;
    case 3: UpscaleBlur1CS(DTid); break;
    case 4: UpscaleBlur2CS(DTid); break;
    case 5: CompositeCS(DTid); break;
    }
}
