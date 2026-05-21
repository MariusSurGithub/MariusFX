# MariusFX — Roadmap ENB-Style Integration

**Objectif** : Intégrer les effets (AO, Bloom, SSR) **dans le pipeline de rendu natif RAGE**, comme ENB, au lieu de les appliquer en post-process.

---

## Architecture cible (ENB-style)

```
┌─────────────────────────────────────────────────────────────┐
│ RAGE GBuffer Pass (opaque geometry)                        │
│ - MRT 0: HDR Irradiance (R16G16B16A16_FLOAT)               │
│ - MRT 1: Albedo (R8G8B8A8_UNORM)                           │
│ - MRT 2: Normal (R10G10B10A2_UNORM, octahedral)            │
│ - MRT 3: Specular (R8G8B8A8_UNORM)                         │
│ - MRT 4: Motion Vectors (R16G16_FLOAT)                     │
│ - Depth-Stencil: Reversed-Z + stencil tags                 │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ ✨ INJECT: MariusFX SSAO Compute Shader                    │
│ - Input: GBuffer Normal + Depth                            │
│ - Output: Write AO factor to HDR buffer (multiply)         │
│ - Timing: Hooked in OMSetRenderTargets when GBuffer ends   │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ RAGE Deferred Lighting Pass                                │
│ - Reads GBuffer, applies lighting                          │
│ - AO already baked into HDR buffer → natural occlusion     │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ RAGE Forward Rendering (particles, transparents, water)    │
│ - Rendered on top of lit scene                             │
│ - NOT affected by AO (correct behavior)                    │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ ✨ INJECT: MariusFX Bloom (pre-tonemap, HDR)               │
│ - Input: HDR buffer (still in linear space)                │
│ - Bright-pass filter + blur + add back                     │
│ - Timing: Hooked before PSSetShader(tonemap_ps_hash)       │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ RAGE Tone Mapping (Uncharted 2 operator)                   │
│ - HDR → LDR conversion                                      │
│ - Bloom already in HDR buffer → natural glow               │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ RAGE UI Rendering (Scaleform + NUI/CEF)                    │
│ - Drawn on final LDR image                                 │
│ - Stencil buffer marks UI pixels                           │
└─────────────────────────────────────────────────────────────┘
                            ↓
                        Present()
```

---

## Phase 1 : SSAO Injection (IN PROGRESS)

### Objectif
Injecter un compute shader SSAO **après** le GBuffer pass, **avant** le lighting pass.

### Détection du point d'injection
**Trigger** : `OMSetRenderTargets` appelé avec `num_views < 4` **après** qu'on ait détecté un GBuffer pass (4+ MRTs).

Cela signifie que RAGE a **terminé** de remplir le GBuffer et passe à la phase suivante (lighting ou autre).

### Implémentation

#### 1. Compute Shader SSAO
**Fichier** : `mariusfx/effects/ssao_inject.hlsl`

```hlsl
// SSAO compute shader — injected into RAGE pipeline
// Reads GBuffer Normal + Depth, writes AO to HDR buffer

Texture2D<float4> tNormal   : register(t0); // GBuffer Normal (octahedral)
Texture2D<float>  tDepth    : register(t1); // Depth buffer
RWTexture2D<float4> uHDR    : register(u0); // HDR buffer (read-write)

cbuffer Params : register(b0)
{
    float2 InvResolution;
    float  SampleRadius;
    float  Intensity;
    uint   SampleCount;
};

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float2 uv = (DTid.xy + 0.5) * InvResolution;
    
    // Decode normal from GBuffer
    float2 oct = tNormal[DTid.xy].rg;
    float3 normal = DecodeOctahedral(oct);
    
    // Get depth
    float depth = tDepth[DTid.xy];
    
    // Compute SSAO (simplified HBAO algorithm)
    float ao = 1.0;
    // ... sampling logic ...
    
    // Multiply HDR buffer by AO factor (darken occluded areas)
    float4 hdr = uHDR[DTid.xy];
    uHDR[DTid.xy] = hdr * ao;
}
```

#### 2. Hook dans gbuffer_capture.cpp
**Fichier** : `mariusfx/gbuffer_capture/gbuffer_capture.cpp`

Ajouter dans `on_omset_rt()` :

```cpp
// ── Case 2: Exiting GBuffer pass (inject AO) ──────────────────────
if (g_gbuffer_pass_active && num_views < 4)
{
    // GBuffer pass just ended — inject SSAO before RAGE continues
    inject_ssao_pass(ctx);
    
    g_gbuffer_pass_active = false;
}
```

Fonction `inject_ssao_pass()` :

```cpp
void inject_ssao_pass(ID3D11DeviceContext *ctx)
{
    // 1. Get device
    ID3D11Device *device = nullptr;
    ctx->GetDevice(&device);
    
    // 2. Bind GBuffer SRVs (Normal, Depth) to compute shader
    ID3D11ShaderResourceView *srvs[] = {
        g_copy_srv[GBUF_NORMAL],
        /* depth SRV from DSV */
    };
    ctx->CSSetShaderResources(0, 2, srvs);
    
    // 3. Bind HDR buffer as UAV (read-write)
    ID3D11UnorderedAccessView *uav = /* create UAV from g_source_res[GBUF_HDR] */;
    ctx->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    
    // 4. Dispatch compute shader
    ctx->CSSetShader(g_ssao_cs, nullptr, 0);
    ctx->Dispatch((g_bb_width + 7) / 8, (g_bb_height + 7) / 8, 1);
    
    // 5. Unbind resources
    ID3D11ShaderResourceView *null_srvs[] = { nullptr, nullptr };
    ctx->CSSetShaderResources(0, 2, null_srvs);
    ID3D11UnorderedAccessView *null_uav = nullptr;
    ctx->CSSetUnorderedAccessViews(0, 1, &null_uav, nullptr);
    
    device->Release();
}
```

#### 3. Compiler le compute shader au runtime
Utiliser `D3DCompile` pour compiler `ssao_inject.hlsl` au démarrage de MariusFX.

---

## Phase 2 : Bloom Injection (TODO)

### Détection du point d'injection
**Trigger** : `PSSetShader` appelé avec le **hash du pixel shader de tonemap** de RAGE.

1. **Identifier le PS hash** : Utiliser RenderDoc ou hook `PSSetShader`, logger tous les hashes, identifier celui du tonemap (appelé 1x par frame, juste avant Present).

2. **Hook avant** : Quand ce PS est sur le point d'être bindé, injecter le bloom pass.

### Implémentation
- Downscale HDR buffer (1/4, 1/8, 1/16)
- Gaussian blur (compute shader, separable)
- Upscale et add back au HDR buffer
- Laisser RAGE faire le tonemap

---

## Phase 3 : Stencil-Based UI Masking (TODO)

### Objectif
Utiliser le **stencil buffer natif de RAGE** au lieu du BB-diff.

### Stencil values RAGE (confirmés)
- `0x89` : Player character
- `0x82` : Player vehicle
- `0x01` : NPCs
- `0x02` : Other vehicles
- `0x03` : Vegetation
- `0x07` : Sky
- `0x00` : UI/NUI (probablement)

### Implémentation
1. **Lire le stencil buffer** avant d'injecter AO/Bloom
2. **Skip les pixels** où `stencil == 0x00` (UI)
3. Optionnel : Skip aussi `stencil == 0x89` si on veut exclure le player de l'AO

---

## Avantages de cette approche

✅ **AO appliqué avant les particules** → plus d'artifacts sur le feu/fumée  
✅ **Bloom en HDR** (avant tonemap) → résultat plus naturel, comme ENB  
✅ **UI naturellement protégée** via stencil buffer  
✅ **Performance** : Compute shaders plus rapides que pixel shaders fullscreen  
✅ **Intégration native** : Les effets font partie du pipeline RAGE, pas post-process  

---

## Risques et limitations

⚠️ **Complexité** : Beaucoup plus complexe que ReShade vanilla  
⚠️ **Compatibilité** : Peut casser avec les updates de GTA V/FiveM  
⚠️ **Debugging** : Plus difficile à debugger (RenderDoc requis)  
⚠️ **UAV sur GBuffer** : RAGE peut ne pas avoir créé les textures avec `D3D11_BIND_UNORDERED_ACCESS` → il faudra peut-être **recréer** les textures  

---

## Prochaines étapes

1. ✅ Détecter le GBuffer pass (DONE via `gbuffer_capture`)
2. ⏳ Créer le compute shader SSAO (`ssao_inject.hlsl`)
3. ⏳ Implémenter `inject_ssao_pass()` dans `gbuffer_capture.cpp`
4. ⏳ Tester in-game : vérifier que l'AO est appliqué avant les particules
5. ⏳ Identifier le PS hash du tonemap de RAGE
6. ⏳ Implémenter le bloom injection (Phase 2)
7. ⏳ Migrer vers stencil masking (Phase 3)

---

## Références

- **ENB Series** : http://enbdev.com (Boris Vorontsov)
- **Adrian Courrèges — GTA V Graphics Study** : http://www.adriancourreges.com/blog/2015/11/02/gta-v-graphics-study/
- **RAGE GBuffer layout** : https://github.com/viclw17/random_notes/blob/master/gta.md
- **D3D11 Compute Shaders** : https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-compute-shader
