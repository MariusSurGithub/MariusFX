# 🚀 MariusFX Transpiler — Roadmap Complète

## Vision

**Convertir automatiquement TOUS les shaders ReShade `.fx` en injections pipeline RAGE.**

Au lieu de tourner en post-process (après tout le rendu), les shaders sont :
1. **Parsés** et **classifiés** automatiquement
2. **Convertis** en compute shaders
3. **Injectés** au bon moment du pipeline RAGE (avant particules, avant UI, etc.)

**Résultat** : Zéro artifacts, performance optimale, qualité ENB-style.

---

## Architecture (4 Modules)

```
┌──────────────┐
│  FX Parser   │  Parse .fx → extract techniques, passes, uniforms
└──────┬───────┘
       ↓
┌──────────────┐
│ Classifier   │  Detect type (AO/Bloom/etc.) → assign priority
└──────┬───────┘
       ↓
┌──────────────┐
│  Transpiler  │  Convert PS → CS (tex2D → Load, SV_Target → UAV)
└──────┬───────┘
       ↓
┌──────────────┐
│  Scheduler   │  Sort by priority → inject at correct pipeline stage
└──────────────┘
```

---

## Phase 1 : FX Parser (Semaine 1)

### Objectif
Parser les fichiers `.fx` (HLSL + annotations ReShade) et extraire :
- Techniques et passes
- Pixel shader entry points
- Uniforms (paramètres UI)
- Textures et samplers

### Implémentation

**Fichier** : `mariusfx/transpiler/fx_parser.cpp`

**Stratégie** :
1. **Regex-based parsing** pour les annotations simples (`uniform float`, `texture`, `sampler`, `technique`)
2. **HLSL preprocessor** : expand `#include`, `#define`, `#if`
3. **Function extraction** : locate shader entry points via pattern matching

**Exemple** :
```cpp
// Input: MXAO.fx
uniform float fMXAOAmbientOcclusionAmount <
    ui_type = "slider";
    ui_min = 0.0; ui_max = 3.0;
    ui_label = "Ambient Occlusion Amount";
> = 1.0;

texture texMXAO { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R8; };
sampler sMXAO { Texture = texMXAO; };

float4 PS_AO(float4 vpos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    // ... SSAO logic ...
    return ao;
}

technique MXAO
{
    pass { PixelShader = PS_AO; }
}

// Output: ParsedFX structure
ParsedFX {
    uniforms: [{ name: "fMXAOAmbientOcclusionAmount", type: "float", default: "1.0", ui_min: 0.0, ui_max: 3.0 }]
    textures: [{ name: "texMXAO", width: 0 (BUFFER_WIDTH), height: 0, format: "R8" }]
    samplers: [{ name: "sMXAO", texture: "texMXAO" }]
    techniques: [{ name: "MXAO", passes: [{ pixel_shader: "PS_AO" }] }]
    shader_functions: { "PS_AO": "float4 PS_AO(...) { ... }" }
}
```

**Tests** :
- Parse `MXAO.fx`, `PPFX_SSDO.fx`, `NeoBloom.fx`
- Vérifier que tous les uniforms/textures sont extraits
- Valider les multi-pass techniques (e.g., SMAA = 3 passes)

**Durée estimée** : 2-3 jours

---

## Phase 2 : Shader Classifier (Semaine 1)

### Objectif
Détecter automatiquement le type de shader et assigner une priorité pipeline.

### Stratégie de Classification

**Pattern Matching** sur :
1. **Nom du shader** : `"MXAO"` → AO, `"Bloom"` → Bloom, `"SMAA"` → AA
2. **Uniforms** : `"occlusion"` → AO, `"threshold"` + `"intensity"` → Bloom
3. **Textures** : `ReShade::DepthBuffer` → depth-dependent (AO, DOF, etc.)
4. **HLSL keywords** : `GetLinearizedDepth()` → depth-based, `saturate(color - threshold)` → bloom

**Exemple** :
```cpp
// Input: ParsedFX from MXAO.fx
ShaderClassification classify_shader(fx) {
    // Check name
    if (contains(fx.filename, "MXAO") || contains(fx.filename, "SSAO"))
        return { type: AO, priority: 0, needs_depth: true };
    
    // Check uniforms
    for (auto &u : fx.uniforms) {
        if (contains(u.name, "occlusion") || contains(u.name, "AO"))
            return { type: AO, priority: 0, needs_depth: true };
    }
    
    // Check shader source
    auto ps_source = fx.shader_functions["PS_AO"];
    if (contains(ps_source, "GetLinearizedDepth"))
        return { type: AO, priority: 0, needs_depth: true };
    
    return { type: UNKNOWN, priority: 999 };
}

// Output: ShaderClassification
{
    type: AMBIENT_OCCLUSION,
    priority: 0,
    needs_gbuffer_normal: true,
    needs_depth: true,
    is_hdr: false,
    confidence: 0.95,
    reason: "Detected 'MXAO' in filename + 'GetLinearizedDepth' in shader"
}
```

**Règles de Priorité** :
```cpp
AO                  → Priority 0   (après GBuffer, avant lighting)
SSGI                → Priority 10  (après AO)
SSR                 → Priority 100 (après lighting)
Bloom               → Priority 150 (avant tonemap, HDR)
Tonemap             → Priority 200 (custom tonemaps)
Color Grading       → Priority 250 (après tonemap, LDR)
AA (SMAA/FXAA)      → Priority 300 (après color grading)
Sharpen             → Priority 350 (après AA)
DOF                 → Priority 360 (après sharpen)
Grain/Vignette      → Priority 400 (cosmetic, avant UI)
```

**Tests** :
- Classifier 20+ shaders populaires
- Vérifier que les priorités sont cohérentes
- Tester les cas ambigus (e.g., shader qui fait AO + Bloom)

**Durée estimée** : 1-2 jours

---

## Phase 3 : HLSL Transpiler (Semaine 2-3)

### Objectif
Convertir les pixel shaders en compute shaders.

### Transformations Principales

#### 1. Signature de fonction
```hlsl
// AVANT (Pixel Shader)
float4 PS_Main(float4 vpos : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
    // ...
}

// APRÈS (Compute Shader)
[numthreads(8, 8, 1)]
void CS_Main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 pixel = DTid.xy;
    float2 texcoord = (pixel + 0.5) / float2(g_screen_width, g_screen_height);
    // ...
}
```

#### 2. Texture sampling
```hlsl
// AVANT
float4 color = tex2D(samplerColor, texcoord);

// APRÈS
float4 color = texColor.Load(int3(pixel, 0));
// OU (si filtering nécessaire)
float4 color = texColor.SampleLevel(samplerColor, texcoord, 0);
```

#### 3. Output
```hlsl
// AVANT
return color;

// APRÈS
uavOutput[pixel] = color;
```

#### 4. Uniforms → Constant Buffer
```hlsl
// AVANT
uniform float fIntensity;
uniform float fRadius;

// APRÈS
cbuffer CB_Params : register(b0)
{
    float fIntensity;
    float fRadius;
    uint2 g_screen_size;  // Ajouté automatiquement
};
```

### Cas Complexes

#### Multi-pass techniques
```hlsl
// MXAO = 2 passes : AO generation + spatial blur
technique MXAO {
    pass AO_Gen { PixelShader = PS_AO; RenderTarget = texAO; }
    pass Blur   { PixelShader = PS_Blur; }
}

// Transpilé en 2 compute shaders :
// CS_AO_Gen  : écrit dans uavAO
// CS_Blur    : lit srvAO, écrit dans uavOutput
```

#### Depth-dependent shaders
```hlsl
// Utilise ReShade::GetLinearizedDepth()
float depth = ReShade::GetLinearizedDepth(texcoord);

// Transpilé en :
float raw_depth = texDepth.Load(int3(pixel, 0)).r;
float depth = linearize_depth(raw_depth);  // Helper function injectée
```

#### Temporal shaders (TAA, motion blur)
```hlsl
// Nécessite previous frame
float4 prev = tex2D(samplerPrevFrame, texcoord - motion);

// Transpilé en :
// - Créer une texture persistante (g_prev_frame_tex)
// - À la fin du frame, copier output → prev
```

### Tests
- Transpiler `MXAO.fx` → vérifier que le CS compile
- Transpiler `NeoBloom.fx` (multi-pass) → vérifier les 3 CS
- Transpiler `SMAA.fx` (complexe, edge detection) → vérifier la qualité

**Durée estimée** : 5-7 jours

---

## Phase 4 : Pipeline Scheduler (Semaine 4)

### Objectif
Gérer l'exécution des shaders transpilés au bon moment du pipeline RAGE.

### Injection Points

```cpp
enum class InjectionPoint {
    AFTER_GBUFFER,   // Hook: OMSetRenderTargets (num_views < 4)
    BEFORE_TONEMAP,  // Hook: PSSetShader (tonemap PS hash)
    AFTER_TONEMAP,   // Hook: PSSetShader (après tonemap)
    AFTER_AA,        // Hook: après SMAA/FXAA (si détecté)
    BEFORE_UI,       // Hook: premier draw sur BB (tl_current_rt0_is_bb)
};
```

### Workflow

1. **Initialization** :
   ```cpp
   PipelineScheduler scheduler;
   scheduler.initialize(device);
   
   // Register all .fx shaders
   for (auto &fx_path : list_fx_files("reshade-shaders/Shaders/")) {
       auto fx = parse_fx(read_file(fx_path), fx_path);
       auto classification = classify_shader(fx);
       scheduler.register_shader(fx, 0, classification);
   }
   ```

2. **Runtime Execution** :
   ```cpp
   // Dans gbuffer_capture::on_omset_rt (Case 2: sortie GBuffer)
   if (g_gbuffer_pass_active && num_views < 4) {
       // ...
       scheduler.execute_at_point(ctx, InjectionPoint::AFTER_GBUFFER, w, h);
   }
   
   // Dans d3d11_device_context::PSSetShader
   if (ps_hash == RAGE_TONEMAP_PS_HASH) {
       scheduler.execute_at_point(ctx, InjectionPoint::BEFORE_TONEMAP, w, h);
       _orig->PSSetShader(pPS, ...);  // Tonemap
       scheduler.execute_at_point(ctx, InjectionPoint::AFTER_TONEMAP, w, h);
   }
   ```

3. **Shader Execution** :
   ```cpp
   void PipelineScheduler::execute_at_point(ctx, point, w, h) {
       for (auto *shader : m_shaders_by_point[point]) {
           if (!shader->enabled) continue;
           
           for (size_t i = 0; i < shader->passes.size(); ++i) {
               auto &pass = shader->compiled_passes[i];
               
               // Bind resources
               ctx->CSSetShader(pass.shader, nullptr, 0);
               ctx->CSSetConstantBuffers(0, 1, &pass.cbuffer);
               ctx->CSSetShaderResources(0, pass.srvs.size(), pass.srvs.data());
               ctx->CSSetUnorderedAccessViews(0, pass.uavs.size(), pass.uavs.data(), nullptr);
               ctx->CSSetSamplers(0, pass.samplers.size(), pass.samplers.data());
               
               // Dispatch
               uint32_t groups_x = (w + 7) / 8;
               uint32_t groups_y = (h + 7) / 8;
               ctx->Dispatch(groups_x, groups_y, 1);
               
               // Unbind UAVs (important!)
               ID3D11UnorderedAccessView *null_uavs[8] = {};
               ctx->CSSetUnorderedAccessViews(0, 8, null_uavs, nullptr);
           }
       }
   }
   ```

### Tests
- Injecter MXAO après GBuffer → vérifier que l'AO est visible
- Injecter NeoBloom avant tonemap → vérifier que le bloom est en HDR
- Injecter Vibrance après tonemap → vérifier les couleurs
- Tester avec 10+ shaders actifs → vérifier l'ordre d'exécution

**Durée estimée** : 3-4 jours

---

## Phase 5 : Integration & Testing (Semaine 5)

### Hook ReShade Effect Loading

**Objectif** : Intercepter le chargement des `.fx` dans ReShade et les transpiler automatiquement.

**Fichier** : `source/effect.cpp` (ReShade core)

**Modification** :
```cpp
// Dans reshade::effect::load()
bool effect::load(const std::filesystem::path &path) {
    // ... code existant ...
    
    // AJOUT : Transpiler hook
    if (mariusfx::transpiler::should_transpile(path)) {
        auto fx = mariusfx::transpiler::parse_fx(source, path.string());
        auto classification = mariusfx::transpiler::classify_shader(fx);
        
        if (classification.confidence > 0.7) {
            // High confidence → transpile and inject
            mariusfx::transpiler::get_scheduler().register_shader(fx, 0, classification);
            
            // DISABLE original .fx execution (éviter double-rendering)
            this->_enabled = false;
            
            reshade::log::message(reshade::log::level::info,
                "[MariusFX Transpiler] Converted '%s' to pipeline injection (%s, priority %d)",
                path.filename().string().c_str(),
                shader_type_name(classification.type),
                classification.priority);
            
            return true;  // Success (transpiled)
        }
    }
    
    // Fallback : load normally as post-process
    // ... code existant ...
}
```

### UI Integration

**Menu MariusFX** : Ajouter un onglet "Pipeline Injection"

```
┌─────────────────────────────────────────────────────────┐
│ MariusFX — Pipeline Injection                           │
├─────────────────────────────────────────────────────────┤
│                                                          │
│ ✅ Auto-Transpile Shaders                               │
│    Convert .fx shaders to pipeline injections           │
│                                                          │
│ Transpiled Shaders (12):                                │
│                                                          │
│ ┌────────────────────────────────────────────────────┐  │
│ │ [✓] MXAO                    AO          0.3ms      │  │
│ │ [✓] PPFX_SSDO               AO          0.4ms      │  │
│ │ [✓] NeoBloom                Bloom       0.8ms      │  │
│ │ [✓] Vibrance                Color       0.1ms      │  │
│ │ [✓] SMAA                    AA          0.5ms      │  │
│ │ [ ] FilmGrain               Cosmetic    0.1ms      │  │
│ └────────────────────────────────────────────────────┘  │
│                                                          │
│ Total Overhead: 2.2ms/frame                             │
│                                                          │
│ [Reload All] [Export Config]                            │
└─────────────────────────────────────────────────────────┘
```

### Performance Profiling

**GPU Timestamps** : Mesurer le temps réel de chaque shader.

```cpp
// Dans PipelineScheduler::execute_at_point
ID3D11Query *query_start, *query_end;
// ... create timestamp queries ...

ctx->End(query_start);
// ... dispatch shader ...
ctx->End(query_end);

// Récupérer les timestamps
uint64_t start_time, end_time;
ctx->GetData(query_start, &start_time, sizeof(uint64_t), 0);
ctx->GetData(query_end, &end_time, sizeof(uint64_t), 0);

shader->last_dispatch_time_ms = (end_time - start_time) / 1000000.0;
```

### Tests Finaux

**Scénarios** :
1. **Preset QuantV** : 10+ shaders actifs → vérifier que tout fonctionne
2. **Stress test** : 30+ shaders → vérifier la stabilité
3. **Particules** : Feu/fumée/explosions → vérifier que l'AO ne bave plus
4. **UI** : Téléphone/HUD → vérifier que les effets ne touchent pas l'UI
5. **Performance** : Comparer avant/après transpilation (frametime)

**Durée estimée** : 3-5 jours

---

## Timeline Totale

| Phase | Durée | Cumul |
|-------|-------|-------|
| Phase 1 : FX Parser | 2-3 jours | 3 jours |
| Phase 2 : Classifier | 1-2 jours | 5 jours |
| Phase 3 : Transpiler | 5-7 jours | 12 jours |
| Phase 4 : Scheduler | 3-4 jours | 16 jours |
| Phase 5 : Integration | 3-5 jours | 21 jours |

**Total** : ~3-4 semaines de développement intensif.

---

## Risques & Mitigation

### Risque 1 : Shaders non-transpilables
**Exemple** : Shaders utilisant `ddx/ddy` (screen-space derivatives) ne marchent pas en compute shader.

**Mitigation** : Fallback vers post-process pour ces shaders. Afficher un warning dans le log.

### Risque 2 : Performance pire qu'avant
**Exemple** : Certains shaders sont plus rapides en pixel shader qu'en compute shader.

**Mitigation** : Profiling automatique. Si un shader transpilé est > 2× plus lent, désactiver la transpilation.

### Risque 3 : Bugs visuels
**Exemple** : Transpilation incorrecte → artifacts, couleurs fausses, etc.

**Mitigation** : Mode debug avec side-by-side comparison (original PS vs transpiled CS).

---

## Prochaines Étapes IMMÉDIATES

**Aujourd'hui** :
1. ✅ Créer les headers (`fx_parser.hpp`, `shader_classifier.hpp`, etc.)
2. ⏳ Implémenter `fx_parser.cpp` (parsing basique)
3. ⏳ Tester sur `MXAO.fx`

**Demain** :
4. ⏳ Implémenter `shader_classifier.cpp`
5. ⏳ Tester classification sur 10 shaders

**Semaine prochaine** :
6. ⏳ Implémenter `hlsl_transpiler.cpp` (cas simple : single-pass, no temporal)
7. ⏳ Tester transpilation sur `MXAO.fx`

---

## Questions ?

- Veux-tu que je commence **maintenant** par Phase 1 (FX Parser) ?
- Ou préfères-tu d'abord **valider** l'approche avec un prototype minimal ?
- Ou autre chose ?

Dis-moi et je fonce ! 🚀
