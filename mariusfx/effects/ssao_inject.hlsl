/**
 * MariusFX — SSAO Compute Shader (Pipeline Injection)
 * 
 * This compute shader is injected AFTER the GBuffer pass, BEFORE lighting.
 * It reads native RAGE GBuffer (Normal + Depth) and writes AO directly
 * into the HDR irradiance buffer, eliminating post-process artifacts.
 * 
 * Technique: Horizon-Based Ambient Occlusion (HBAO) simplified for performance.
 * 
 * Author: MariusFX Team
 * License: MIT
 */

// ─── Input Textures (GBuffer) ────────────────────────────────────────────────

Texture2D<float4> tGBufferNormal   : register(t0); // R10G10B10A2_UNORM (octahedral)
Texture2D<float>  tDepthBuffer     : register(t1); // R32_FLOAT (reversed-Z)

// ─── Output Texture (HDR Buffer, Read-Write) ─────────────────────────────────

RWTexture2D<float4> uHDRBuffer : register(u0); // R16G16B16A16_FLOAT

// ─── Constant Buffer (Parameters) ────────────────────────────────────────────

cbuffer Params : register(b0)
{
    float2 InvResolution;      // 1.0 / (width, height)
    float  SampleRadius;       // World-space radius (e.g., 0.5)
    float  Intensity;          // AO intensity multiplier (e.g., 1.5)
    uint   SampleCount;        // Number of samples per pixel (e.g., 8)
    float  DepthFadeStart;     // Depth where AO starts fading (e.g., 0.9)
    float  DepthFadeEnd;       // Depth where AO is fully faded (e.g., 0.99)
    float  BiasAngle;          // Bias to avoid self-occlusion (radians, e.g., 0.1)
};

// ─── Helpers ─────────────────────────────────────────────────────────────────

// Decode octahedral normal (RAGE GBuffer encoding)
float3 DecodeOctahedral(float2 oct)
{
    oct = oct * 2.0 - 1.0;
    float3 n = float3(oct.x, oct.y, 1.0 - abs(oct.x) - abs(oct.y));
    float t = saturate(-n.z);
    n.xy += (n.xy >= 0.0) ? -t : t;
    return normalize(n);
}

// Convert UV + depth to view-space position
// Assumes reversed-Z (far=0, near=1) and perspective projection
float3 UVToViewPos(float2 uv, float depth)
{
    // GTA V uses reversed-Z logarithmic depth
    // For simplicity, we treat it as linear here (good enough for AO)
    float z = depth * 1000.0; // Far plane ~1000 units
    
    // NDC coordinates ([-1,1] range)
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y; // Flip Y (D3D convention)
    
    // Aspect ratio ~16:9 (hardcoded for now, could be a cbuffer param)
    float aspect = 1920.0 / 1080.0;
    
    // Reconstruct view-space position (simplified perspective)
    return float3(ndc.x * z * aspect, ndc.y * z, z);
}

// Hash function for random rotation per pixel (reduces banding)
float Hash(uint2 p)
{
    uint h = p.x * 374761393u + p.y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float(h) * (1.0 / 4294967296.0);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // Early exit if out of bounds
    uint2 dims;
    uHDRBuffer.GetDimensions(dims.x, dims.y);
    if (DTid.x >= dims.x || DTid.y >= dims.y)
        return;
    
    float2 uv = (DTid.xy + 0.5) * InvResolution;
    
    // ── Read GBuffer ─────────────────────────────────────────────────────────
    
    float4 normalSample = tGBufferNormal[DTid.xy];
    float depth = tDepthBuffer[DTid.xy];
    
    // Check if GBuffer data is valid (non-zero normal)
    if (dot(normalSample.rgb, normalSample.rgb) < 0.0001)
    {
        // No geometry here (sky, UI, etc.) — skip AO
        return;
    }
    
    // Decode normal from octahedral encoding
    float3 normal = DecodeOctahedral(normalSample.rg);
    normal.z = -normal.z; // RAGE normals: flip Z to point towards viewer
    
    // Reconstruct view-space position
    float3 viewPos = UVToViewPos(uv, depth);
    
    // ── Depth fade (skip AO on distant objects) ─────────────────────────────
    
    float depthFade = 1.0 - smoothstep(DepthFadeStart, DepthFadeEnd, depth);
    if (depthFade < 0.01)
        return; // Too far, skip AO
    
    // ── HBAO Sampling ────────────────────────────────────────────────────────
    
    float ao = 0.0;
    
    // Random rotation to reduce banding
    float randomAngle = Hash(DTid.xy) * 6.28318530718; // 2*PI
    float cosRand = cos(randomAngle);
    float sinRand = sin(randomAngle);
    
    // Sample in a circle around the pixel
    for (uint i = 0; i < SampleCount; ++i)
    {
        float angle = (float(i) / float(SampleCount)) * 6.28318530718 + randomAngle;
        float2 sampleDir = float2(cos(angle), sin(angle));
        
        // Apply random rotation
        float2 rotatedDir = float2(
            sampleDir.x * cosRand - sampleDir.y * sinRand,
            sampleDir.x * sinRand + sampleDir.y * cosRand
        );
        
        // Sample offset in screen space
        float2 sampleUV = uv + rotatedDir * SampleRadius * InvResolution;
        
        // Clamp to screen bounds
        if (any(sampleUV < 0.0) || any(sampleUV > 1.0))
            continue;
        
        // Sample depth at offset
        uint2 sampleCoord = uint2(sampleUV / InvResolution);
        float sampleDepth = tDepthBuffer[sampleCoord];
        
        // Reconstruct sample position
        float3 samplePos = UVToViewPos(sampleUV, sampleDepth);
        
        // Vector from center to sample
        float3 diff = samplePos - viewPos;
        float dist = length(diff);
        
        // Avoid division by zero
        if (dist < 0.001)
            continue;
        
        float3 sampleDir3D = diff / dist;
        
        // Horizon angle (angle between normal and sample direction)
        float horizonAngle = dot(normal, sampleDir3D);
        
        // Apply bias to avoid self-occlusion
        horizonAngle = max(horizonAngle - BiasAngle, 0.0);
        
        // Distance falloff (closer samples contribute more)
        float falloff = 1.0 - smoothstep(0.0, SampleRadius, dist);
        
        // Accumulate occlusion
        ao += horizonAngle * falloff;
    }
    
    // Normalize AO by sample count
    ao /= float(SampleCount);
    
    // Apply intensity and invert (1.0 = no occlusion, 0.0 = full occlusion)
    ao = 1.0 - saturate(ao * Intensity);
    
    // Apply depth fade
    ao = lerp(1.0, ao, depthFade);
    
    // ── Write to HDR Buffer ──────────────────────────────────────────────────
    
    // Read current HDR value
    float4 hdr = uHDRBuffer[DTid.xy];
    
    // Multiply by AO (darken occluded areas)
    // This is the key: we modify the HDR buffer BEFORE lighting,
    // so the AO is baked into the final image naturally.
    uHDRBuffer[DTid.xy] = hdr * ao;
}
