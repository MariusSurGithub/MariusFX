# Phase 1 — SSAO Injection TODO

## ✅ Fait (Session actuelle)

1. **Compute Shader SSAO** (`mariusfx/effects/ssao_inject.hlsl`)
   - HBAO algorithm simplifié
   - Octahedral normal decoding
   - Random rotation per-pixel (anti-banding)
   - Depth fade pour objets lointains
   - Bias angle pour éviter self-occlusion

2. **SSAO Injector** (`mariusfx/effects/ssao_injector.cpp/.hpp`)
   - Compilation du CS au runtime (D3DCompile)
   - Constant buffer pour les paramètres
   - API publique : `initialize()`, `inject_ssao_pass()`, `shutdown()`
   - Paramètres configurables (radius, intensity, sample count, etc.)

3. **Intégration dans gbuffer_capture**
   - Include de `ssao_injector.hpp`
   - Appel prévu après la copie des GBuffer textures
   - **Commenté pour l'instant** (voir TODO ci-dessous)

4. **Ajout au projet Visual Studio**
   - `ssao_injector.cpp` et `.hpp` ajoutés à `ReShade.vcxproj`

---

## ⏳ TODO — Prochaines étapes

### 1. **Créer un UAV pour le HDR buffer** (CRITIQUE)

**Problème** : RAGE crée le HDR buffer avec `D3D11_BIND_RENDER_TARGET` uniquement, **pas** `D3D11_BIND_UNORDERED_ACCESS`. On ne peut donc pas créer un UAV dessus directement.

**Solutions possibles** :

#### Option A : Recréer le HDR buffer avec UAV support (RECOMMANDÉ)
```cpp
// Dans gbuffer_capture.cpp, après avoir détecté le GBuffer pass :

// 1. Récupérer la desc du HDR buffer original
D3D11_TEXTURE2D_DESC hdr_desc;
ID3D11Texture2D *hdr_tex = nullptr;
g_source_res[GBUF_HDR]->QueryInterface(&hdr_tex);
hdr_tex->GetDesc(&hdr_desc);
hdr_tex->Release();

// 2. Créer une copie avec UAV support
hdr_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
ID3D11Texture2D *hdr_uav_tex = nullptr;
device->CreateTexture2D(&hdr_desc, nullptr, &hdr_uav_tex);

// 3. Copier le contenu du HDR original
ctx->CopyResource(hdr_uav_tex, g_source_res[GBUF_HDR]);

// 4. Créer le UAV
D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
uav_desc.Format = hdr_desc.Format;
uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
uav_desc.Texture2D.MipSlice = 0;
ID3D11UnorderedAccessView *hdr_uav = nullptr;
device->CreateUnorderedAccessView(hdr_uav_tex, &uav_desc, &hdr_uav);

// 5. Injecter SSAO
ssao_injector::inject_ssao_pass(ctx, g_copy_srv[GBUF_NORMAL], depth_srv, hdr_uav, w, h);

// 6. Copier le résultat dans le HDR original
ctx->CopyResource(g_source_res[GBUF_HDR], hdr_uav_tex);

// 7. Cleanup
hdr_uav->Release();
hdr_uav_tex->Release();
```

**Avantages** :
- ✅ Fonctionne à coup sûr
- ✅ Pas de modification du pipeline RAGE

**Inconvénients** :
- ❌ 2 copies par frame (original → UAV tex, UAV tex → original)
- ❌ ~0.2-0.3ms de overhead

#### Option B : Hook CreateTexture2D de RAGE (AVANCÉ)
Intercepter `ID3D11Device::CreateTexture2D` et ajouter `D3D11_BIND_UNORDERED_ACCESS` au HDR buffer quand RAGE le crée.

**Avantages** :
- ✅ Pas de copies supplémentaires
- ✅ Performance optimale

**Inconvénients** :
- ❌ Complexe (hook vtable de ID3D11Device)
- ❌ Risque de casser d'autres choses

**Recommandation** : Commencer par **Option A** (copies), optimiser plus tard si nécessaire.

---

### 2. **Créer un SRV pour le Depth Buffer**

Le depth buffer est actuellement bindé comme `ID3D11DepthStencilView`. On a besoin d'un **SRV** pour le lire dans le compute shader.

**Solution** :
```cpp
// Dans gbuffer_capture.cpp, récupérer le DSV du GBuffer pass
// (il est passé en paramètre à OMSetRenderTargets)

ID3D11Resource *depth_res = nullptr;
dsv->GetResource(&depth_res);

// Créer un SRV pour le depth buffer
D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; // ou DXGI_FORMAT_R32_FLOAT si reversed-Z
srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
srv_desc.Texture2D.MipLevels = 1;

ID3D11ShaderResourceView *depth_srv = nullptr;
device->CreateShaderResourceView(depth_res, &srv_desc, &depth_srv);

depth_res->Release();
```

**Note** : Le format dépend du depth buffer de RAGE. Utiliser RenderDoc pour vérifier :
- Si `DXGI_FORMAT_D24_UNORM_S8_UINT` → SRV format = `DXGI_FORMAT_R24_UNORM_X8_TYPELESS`
- Si `DXGI_FORMAT_D32_FLOAT` → SRV format = `DXGI_FORMAT_R32_FLOAT`

---

### 3. **Initialiser le SSAO Injector au démarrage**

Ajouter dans `runtime.cpp` (ou `dll_main.cpp`) :

```cpp
#include "mariusfx/effects/ssao_injector.hpp"

// Dans on_init() ou équivalent :
void on_init(ID3D11Device *device)
{
    if (!mariusfx::ssao_injector::initialize(device))
    {
        reshade::log::message(reshade::log::level::error,
            "[MariusFX] Failed to initialize SSAO injector");
    }
}

// Dans on_shutdown() :
void on_shutdown()
{
    mariusfx::ssao_injector::shutdown();
}
```

---

### 4. **Décommenter l'appel dans gbuffer_capture.cpp**

Une fois les étapes 1-3 complétées, décommenter les lignes 327-337 dans `gbuffer_capture.cpp` :

```cpp
if (g_copy_srv[GBUF_NORMAL] && depth_srv && hdr_uav)
{
    ssao_injector::inject_ssao_pass(
        ctx,
        g_copy_srv[GBUF_NORMAL],
        depth_srv,
        hdr_uav,
        g_bb_width,
        g_bb_height
    );
}
```

---

### 5. **Tester in-game**

1. **Build** le projet
2. **Copier** `dxgi.dll` dans `plugins/`
3. **Lancer** FiveM
4. **Vérifier** dans `ReShade.log` :
   - `[MariusFX SSAO] Initialized successfully`
   - `[MariusFX GBuffer] Detected X-MRT pass`
   - `[MariusFX GBuffer] Captured X buffers`
   - `[MariusFX SSAO] First injection successful`

5. **Vérifier visuellement** :
   - L'AO doit être visible sur la géométrie opaque
   - **PAS d'artifacts** sur le feu/fumée/particules
   - **PAS de halo** autour des peds

---

### 6. **Exposer les paramètres SSAO dans l'UI ReShade** (optionnel)

Créer un addon ReShade ou modifier `runtime_gui.cpp` pour exposer :
- `sample_radius` (slider 0.1 - 2.0)
- `intensity` (slider 0.0 - 3.0)
- `sample_count` (slider 4 - 16)
- `depth_fade_start` (slider 0.5 - 1.0)
- `depth_fade_end` (slider 0.5 - 1.0)
- `bias_angle` (slider 0.0 - 0.5)

---

## 🔧 Debugging Tips

### Si le compute shader ne se compile pas :
- Vérifier `ReShade.log` pour les erreurs de compilation
- Le shader source est embedé dans `ssao_injector.cpp` (ligne ~50)
- Tester la compilation offline avec `fxc.exe` :
  ```
  fxc /T cs_5_0 /E main mariusfx\effects\ssao_inject.hlsl
  ```

### Si l'injection ne se déclenche pas :
- Vérifier que `g_gbuffer_pass_active` devient `true` (log ligne 253)
- Vérifier que `g_gbuffer_draw_count >= 50` (log ligne 315)
- Vérifier que les SRVs/UAV ne sont pas `nullptr`

### Si l'AO est trop fort/faible :
- Ajuster `intensity` dans `ssao_injector.cpp` (ligne ~30)
- Ajuster `sample_radius` (plus grand = AO plus étendu)

### Si l'AO a du banding :
- Augmenter `sample_count` (8 → 12 ou 16)
- La rotation random devrait déjà réduire le banding

---

## 📊 Performance attendue

- **Compute shader dispatch** : ~0.3-0.5ms @ 1080p (8 samples)
- **Copies HDR buffer** : ~0.2ms (si Option A)
- **Total overhead** : ~0.5-0.7ms par frame

Pour comparaison, les shaders AO post-process actuels (MXAO, PPFX_SSDO) prennent ~1-2ms.

---

## 🎯 Objectif final

Une fois Phase 1 terminée, tu auras :
- ✅ AO appliqué **avant** les particules → plus d'artifacts sur le feu
- ✅ AO utilisant les **normals natives** → plus de halo sur les peds
- ✅ Performance **meilleure** que les shaders post-process
- ✅ Base solide pour Phase 2 (Bloom HDR injection)

Ensuite, on pourra désactiver complètement les shaders AO post-process (MXAO, PPFX_SSDO, etc.) et utiliser uniquement l'AO injecté !
