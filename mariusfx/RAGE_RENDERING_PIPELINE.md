# RAGE Rendering Pipeline — GTA V / FiveM

Documentation technique sur l'ordre de rendu et l'identification des couches UI/NUI dans le moteur RAGE de GTA V, basée sur l'analyse d'Adrian Courrèges et les sources CitizenFX.

## Ordre de rendu (frame complète)

```
1. Environment Cubemap
2. Shadow Maps (CSM 4×1024×1024, ~1000 draw calls)
3. ──────────────────────────────────────────────────────────
   GBuffer Fill Pass (~1900 draw calls)
   ──────────────────────────────────────────────────────────
   MRT Slot 0: HDR Irradiance      (R16G16B16A16_FLOAT)
   MRT Slot 1: Diffuse Albedo      (R8G8B8A8_UNORM, alpha=blend mask)
   MRT Slot 2: Normal (octahedral) (R10G10B10A2_UNORM, RG=normal, A=vegetation mask)
   MRT Slot 3: Specular            (R8G8B8A8_UNORM, R=intensity, G=gloss, B=fresnel)
   MRT Slot 4: Motion Vectors      (R16G16_FLOAT)
   Depth-Stencil: Reversed-Z logarithmic (far=0, near=1)
   
   Stencil values:
     0x89 = Player character (ped contrôlé)
     0x82 = Player vehicle (véhicule conduit)
     0x01 = NPCs
     0x02 = Vehicles (autres véhicules)
     0x03 = Vegetation / Foliage
     0x07 = Sky
   ──────────────────────────────────────────────────────────

4. Planar Reflection Map (ocean, ~650 draw calls)
5. Screen Space Ambient Occlusion (SSAO)
6. GBuffer Combination (deferred lighting, HDR output)
7. Subsurface Scattering (player skin only, stencil 0x89)
8. Water rendering
9. Atmosphere (light shafts, sky)
10. Transparent Objects (glass, particles, forward-rendered)
11. Dithering Smoothing
12. ──────────────────────────────────────────────────────────
    Tone Mapping + Bloom (Uncharted 2 filmic operator)
    ──────────────────────────────────────────────────────────
    - HDR → LDR conversion
    - Exposure adaptation (eye simulation)
    - Bright-pass filter → bloom buffer (1/16 downscale + blur)
    - Gamma correction (linear → sRGB)
    ──────────────────────────────────────────────────────────

13. Anti-Aliasing (FXAA si activé)
14. Lens Distortion + Chromatic Aberration
15. ──────────────────────────────────────────────────────────
    UI RAGE Native (Scaleform)
    ──────────────────────────────────────────────────────────
    - Minimap (tuiles vectorisées, scissor test actif)
    - HUD natif (icônes, texte, widgets)
    - Menus pause natifs
    
    Pixel Shader hash (FNV-1a 64): ba41eb5b83fbdedd
    Letterbox/Background PS hash:  cd6646935992c6e7
    ──────────────────────────────────────────────────────────

16. ──────────────────────────────────────────────────────────
    NUI/CEF Overlay (FiveM uniquement)
    ──────────────────────────────────────────────────────────
    - Chromium Embedded Framework (HTML/CSS/JS)
    - Téléphones custom (lb-phone, qs-phone, qb-phone en mode DUI)
    - Menus custom (esx_menu_default, etc.)
    - Overlays fullscreen (notifications, HUD custom)
    
    Pixel Shader hash (FNV-1a 64): 174fd1e7e23b8dba
    HLSL source (NUIWindow.cpp):
      float4 main(PS_INPUT input) : SV_Target {
        float4 color = tx.Sample(sm, input.uv);
        color.rgb /= color.a; // un-premultiply alpha
        return color;
      }
    ──────────────────────────────────────────────────────────

17. Present (IDXGISwapChain::Present)
```

**Total:** ~4155 draw calls, 1113 textures, 88 render targets par frame (scène complexe).

---

## Depth Bias sur les Peds (problème AO)

### Symptôme
Une fine couche (~2-4px) autour des personnages n'est **pas affectée** par l'AO/SSDO, créant un "halo" visible.

### Cause racine
GTA V applique un **depth bias** sur les peds (stencil 0x89, 0x01) pour éviter le Z-fighting avec les vêtements/accessoires. Ce bias décale légèrement la profondeur écrite dans le depth buffer, mais **pas** dans le GBuffer normal.

Quand un shader AO reconstruit les normales depuis le depth buffer (méthode classique `cross(dFdx, dFdy)`), il utilise ce depth biaisé → les normales calculées ne correspondent pas exactement à la géométrie réelle → l'AO rate les pixels de bordure.

### Solution MariusFX
Le module `gbuffer_capture` capture les **normals natives** du GBuffer (MRT slot 2, octahedral-encoded) qui sont **sans bias**. Les shaders AO peuvent alors utiliser ces normals pixel-perfect via `MFX_GetNativeNormal()` au lieu de les reconstruire.

**Résultat:** `edge_weight = 0.0` → plus d'artifact de bordure.

---

## Téléphones FiveM (DUI vs NUI)

### Mode DUI (Direct UI)
Les téléphones modernes (lb-phone, qs-phone, qb-phone) utilisent **DUI** :
1. CEF rend le HTML dans une texture D3D11 (`SharedTexture` ou `UpdateSubresource`)
2. Cette texture est **sampée par les shaders RAGE natifs** du modèle 3D du téléphone
3. Le téléphone est rendu pendant la **passe GBuffer** (étape 3), pas en overlay

**Conséquence:** Le téléphone est naturellement intégré à la scène → l'AO/Bloom s'applique normalement, **pas besoin de masking stencil**.

### Mode NUI (Overlay)
Les anciens téléphones ou menus fullscreen utilisent **NUI overlay** :
- Rendu **après** tous les post-process (étape 16)
- PS hash `174fd1e7e23b8dba`
- Nécessite un masking si on veut éviter que l'AO/Bloom ne "bave" dessus

---

## Masking UI dans MariusFX

### Système actuel : BB-diff (`ui_safe_mask`)
1. **Capture `scene_clean`** au 1er draw sur BB du frame (= blit post-FX, avant UI)
2. **Capture `bb_with_ui`** au début de `on_present` (avant que ReShade ne touche au BB)
3. **Diff per-pixel** : `ui_mask = smoothstep(0.005, 0.05, length(bb_with_ui - scene_clean))`
4. **Restore UI** : `final = lerp(reshade_modified_bb, bb_with_ui, ui_mask)`

**Avantages:**
- Capture **tous** les types d'UI (RAGE native, NUI, RageUI, etc.) sans heuristique fragile
- Pas de dépendance sur des PS hashes spécifiques
- Naturellement anti-aliasé via `smoothstep`

**Coût:** 1 `CopyResource` par frame (~0.1ms @ 1080p)

### Alternative : Stencil tagging (non implémenté)
Si on voulait un masking **avant** le rendu UI (pour que l'AO/Bloom ne calculent même pas sur les pixels UI) :

1. Hook `PSSetShader` pour détecter les PS hashes UI :
   - `174fd1e7e23b8dba` (NUI)
   - `ba41eb5b83fbdedd` (RAGE UI)
   - `cd6646935992c6e7` (RAGE letterbox)

2. Bind un DSS custom qui écrit `stencil = 0x80` pendant ces draws

3. Dans les passes AO/Bloom, bind un DSS qui **teste** `stencil != 0x80`

**Problème:** Nécessite de modifier le stencil buffer **pendant** le rendu de GTA V, risque de conflit avec les stencil values natives (0x89, 0x82, etc.). Le BB-diff est plus sûr.

---

## Références

- **Adrian Courrèges — GTA V Graphics Study:**  
  http://www.adriancourreges.com/blog/2015/11/02/gta-v-graphics-study/

- **CitizenFX — NUIWindow.cpp (PS hash source):**  
  `fivem/code/components/nui-core/src/NUIWindow.cpp`

- **RAGE GBuffer layout (GitHub notes):**  
  https://github.com/viclw17/random_notes/blob/master/gta.md

---

## Notes pour les développeurs de shaders

Si tu crées un shader AO/SSDO/GI pour MariusFX :

1. **Inclure** `MariusFX_GBuffer.fxh` en haut du fichier
2. **Utiliser** `MFX_ShouldUseNativeNormals(uv)` pour tester si les normals natives sont disponibles
3. **Appeler** `MFX_GetNativeNormal(uv)` au lieu de reconstruire depuis le depth
4. **Ajouter** une checkbox `MFX_UseNativeGBuffer` (déjà dans le `.fxh`, automatique)

**Exemple:**
```hlsl
#include "ReShade.fxh"
#include "MariusFX_GBuffer.fxh"

float3 get_normal(float2 uv) {
    if (MFX_ShouldUseNativeNormals(uv))
        return MFX_GetNativeNormal(uv); // Pixel-perfect, no bias
    
    // Fallback: reconstruct from depth
    // ... votre code existant ...
}
```

Le toggle `MFX_UseNativeGBuffer` apparaîtra automatiquement dans la catégorie "MariusFX" de chaque shader.
