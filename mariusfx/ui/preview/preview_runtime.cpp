/*
 * MariusFX preview — concrete impl of the runtime stub.
 *
 * Hard-coded "preset" with a handful of effects + techniques + uniforms
 * resembling what the user typically loads in FiveM (qUINT SSAO, MXAO,
 * FXAA, Bloom, Tonemap, Vignette …). Values are stored in-process so
 * widgets actually move when you tweak them.
 */

#include "preview_runtime.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

// ── Mock data shape ────────────────────────────────────────────────────────
struct MockUniform
{
    uint64_t            id;
    std::string         effect_full;
    std::string         name;
    std::string         label;
    std::string         tooltip;
    std::string         category;
    std::string         ui_type;
    std::string         items;            // already \0-separated
    reshade::api::format base_type;
    uint32_t            rows = 1, cols = 1, arr = 0;
    bool                has_min = false, has_max = false;
    float               ui_min[4] = {}, ui_max[4] = {}, ui_step[4] = {};
    // Live + default values (only one of these arrays is meaningful per type).
    float    fval[4] = {}, default_fval[4] = {};
    int32_t  ival[4] = {}, default_ival[4] = {};
    uint32_t uval[4] = {}, default_uval[4] = {};
    bool     bval = false, default_bval = false;
};

struct MockTechnique
{
    uint64_t      id;
    std::string   effect_full;
    std::string   name;
    bool          enabled = false;
    bool          hidden  = false;
    // Pretend per-frame cost in nanoseconds (animated in get_timing).
    uint64_t      gpu_ns_base = 0;
    uint64_t      cpu_ns_base = 0;
};

struct MockState
{
    std::vector<MockTechnique> techs;
    std::vector<MockUniform>   uniforms;

    bool   effects_on        = true;
    bool   performance_mode  = false;
    char   preset_path[260]  = "C:/games/FiveM/reshade-shaders/Presets/MariusFX_Quality.ini";

    // For technique timing animation.
    double anim_t = 0.0;
};

MockState &state()
{
    static MockState s;
    return s;
}

// ── Small builder helpers ──────────────────────────────────────────────────
uint64_t next_id()
{
    static uint64_t c = 0x1000;
    return ++c;
}

MockTechnique &add_tech(const char *eff, const char *name, bool enabled,
                        uint64_t gpu_ns, uint64_t cpu_ns)
{
    auto &t = state().techs.emplace_back();
    t.id          = next_id();
    t.effect_full = eff;
    t.name        = name;
    t.enabled     = enabled;
    t.gpu_ns_base = gpu_ns;
    t.cpu_ns_base = cpu_ns;
    return t;
}

MockUniform &add_float(const char *eff, const char *name, const char *label,
                       float def, float lo, float hi,
                       const char *category = "", const char *tooltip = "")
{
    auto &u = state().uniforms.emplace_back();
    u.id           = next_id();
    u.effect_full  = eff;
    u.name         = name;
    u.label        = label;
    u.category     = category;
    u.tooltip      = tooltip;
    u.ui_type      = "slider";
    u.base_type    = reshade::api::format::r32_float;
    u.rows         = 1; u.cols = 1;
    u.has_min = u.has_max = true;
    u.ui_min[0] = lo; u.ui_max[0] = hi;
    u.ui_step[0] = (hi - lo) * 0.01f;
    u.fval[0] = u.default_fval[0] = def;
    return u;
}

MockUniform &add_bool(const char *eff, const char *name, const char *label,
                      bool def, const char *category = "", const char *tooltip = "")
{
    auto &u = state().uniforms.emplace_back();
    u.id           = next_id();
    u.effect_full  = eff;
    u.name         = name;
    u.label        = label;
    u.category     = category;
    u.tooltip      = tooltip;
    u.ui_type      = "";
    u.base_type    = reshade::api::format::r32_typeless; // booleans
    u.rows         = 1; u.cols = 1;
    u.bval = u.default_bval = def;
    return u;
}

MockUniform &add_int(const char *eff, const char *name, const char *label,
                     int def, int lo, int hi,
                     const char *category = "", const char *tooltip = "")
{
    auto &u = state().uniforms.emplace_back();
    u.id           = next_id();
    u.effect_full  = eff;
    u.name         = name;
    u.label        = label;
    u.category     = category;
    u.tooltip      = tooltip;
    u.ui_type      = "slider";
    u.base_type    = reshade::api::format::r32_sint;
    u.rows         = 1; u.cols = 1;
    u.has_min = u.has_max = true;
    u.ui_min[0] = (float)lo; u.ui_max[0] = (float)hi;
    u.ui_step[0] = 1.0f;
    u.ival[0] = u.default_ival[0] = def;
    return u;
}

MockUniform &add_combo(const char *eff, const char *name, const char *label,
                       int def, const char *items_z, const char *category = "")
{
    auto &u = state().uniforms.emplace_back();
    u.id           = next_id();
    u.effect_full  = eff;
    u.name         = name;
    u.label        = label;
    u.category     = category;
    u.ui_type      = "combo";
    // Items: "First\\0Second\\0Third\\0" — ui.cpp converts the literal "\\0"
    // sequence into real null bytes.
    u.items        = items_z;
    u.base_type    = reshade::api::format::r32_sint;
    u.rows         = 1; u.cols = 1;
    u.ival[0] = u.default_ival[0] = def;
    return u;
}

MockUniform &add_color(const char *eff, const char *name, const char *label,
                       float r, float g, float b, const char *category = "")
{
    auto &u = state().uniforms.emplace_back();
    u.id           = next_id();
    u.effect_full  = eff;
    u.name         = name;
    u.label        = label;
    u.category     = category;
    u.ui_type      = "color";
    u.base_type    = reshade::api::format::r32_float;
    u.rows         = 3; u.cols = 1;
    u.fval[0] = u.default_fval[0] = r;
    u.fval[1] = u.default_fval[1] = g;
    u.fval[2] = u.default_fval[2] = b;
    return u;
}

void seed_state()
{
    auto &s = state();
    if (!s.techs.empty()) return;

    // ───────────────────────────────────────────────────────────────────
    // Full canonical ReShade shader catalogue (crosire/reshade-shaders +
    // qUINT pack + Marty McFly + popular community shaders). 100+ techs.
    // The production DLL enumerates whatever the user actually loaded —
    // there is no client-side cap. This mock reproduces the variety so
    // sidebar layout / sections / search / filters get a real workout.
    // ───────────────────────────────────────────────────────────────────

    const char *base = "C:/games/FiveM/reshade-shaders/Shaders/";
    auto P = [base](const char *sub) {
        thread_local std::string buf;
        buf = std::string(base) + sub;
        return buf.c_str();
    };

    // ── Bulk catalogue ────────────────────────────────────────────────
    // (file, technique name, enabled, gpu_ns, cpu_ns)
    struct E { const char *file; const char *name; bool on; uint64_t gpu_ns, cpu_ns; };
    static const E table[] = {
        // crosire/reshade-shaders — Effects/
        { "3DFX.fx",                          "3DFX",                  false, 180'000, 14'000 },
        { "AdaptiveFog.fx",                   "Adaptive fog",          false, 220'000, 17'000 },
        { "AdaptiveSharpen.fx",               "Adaptive sharpen",      false, 240'000, 18'000 },
        { "AmbientLight.fx",                  "AmbientLight",          true,  340'000, 27'000 },
        { "ASCII.fx",                         "ASCII",                 false, 130'000, 11'000 },
        { "AspectRatioBars.fx",               "Aspect-ratio bars",     false,  60'000,  5'000 },
        { "Bloom.fx",                         "Bloom",                 true,  920'000, 70'000 },
        { "BlueNoiseDither.fx",               "Blue-noise dither",     true,  100'000,  9'000 },
        { "Border.fx",                        "Border",                false,  60'000,  6'000 },
        { "Cartoon.fx",                       "Cartoon",               false, 150'000, 12'000 },
        { "ChromaticAberration.fx",           "Chromatic aberration",  false, 110'000, 10'000 },
        { "Clarity.fx",                       "Clarity",               false, 260'000, 19'000 },
        { "ColorIsolation.fx",                "Color isolation",       false, 120'000, 10'000 },
        { "ColorMatrix.fx",                   "Color matrix",          false,  95'000,  8'500 },
        { "Colourfulness.fx",                 "Colourfulness",         false,  95'000,  8'200 },
        { "CompositingSwitch.fx",             "Compositing switch",    false,  55'000,  5'000 },
        { "ContrastAdaptiveSharpen.fx",       "CAS",                   true,  220'000, 16'000 },
        { "CRT.fx",                           "CRT",                   false, 280'000, 21'000 },
        { "Curves.fx",                        "Curves",                true,  170'000, 13'000 },
        { "Daltonize.fx",                     "Daltonize",             false,  95'000,  8'000 },
        { "Deband.fx",                        "Deband",                true,  240'000, 18'000 },
        { "Denoise.fx",                       "Denoise",               false, 380'000, 29'000 },
        { "DepthHaze.fx",                     "Depth haze",            false, 165'000, 13'500 },
        { "DisplayDepth.fx",                  "Display depth",         false,  90'000,  8'000 },
        { "DPX.fx",                           "DPX",                   false, 140'000, 11'500 },
        { "Emphasize.fx",                     "Emphasize",             false, 200'000, 15'000 },
        { "EyeAdaption.fx",                   "Eye adaptation",        true,  160'000, 13'200 },
        { "FakeHDR.fx",                       "Fake HDR",              false, 220'000, 17'000 },
        { "FakeMotionBlur.fx",                "Fake motion blur",      false, 320'000, 24'000 },
        { "FilmGrain.fx",                     "Film grain",            false, 130'000, 18'000 },
        { "FilmGrain2.fx",                    "Film grain 2",          false, 135'000, 12'000 },
        { "FilmicAnamorphSharpen.fx",         "Filmic anamorph sharp", false, 210'000, 15'000 },
        { "FilmicPass.fx",                    "Filmic pass",           false, 175'000, 13'500 },
        { "FineSharp.fx",                     "FineSharp",             false, 195'000, 14'000 },
        { "Fisheye.fx",                       "Fisheye",               false, 130'000, 11'200 },
        { "FXAA.fx",                          "FXAA",                  true,  410'000, 35'000 },
        { "GaussianBlur.fx",                  "Gaussian blur",         false, 280'000, 21'000 },
        { "GlitchB.fx",                       "GlitchB",               false,  90'000,  8'000 },
        { "HDR.fx",                           "HDR",                   false, 240'000, 18'000 },
        { "HighPassSharpen.fx",               "High-pass sharpen",     false, 240'000, 17'000 },
        { "HQ4X.fx",                          "HQ4X",                  false, 360'000, 27'000 },
        { "HSLShift.fx",                      "HSL shift",             false, 140'000, 11'500 },
        { "KNearestNeighbors.fx",             "K-Nearest Neighbors",   false, 460'000, 32'000 },
        { "LeveLines.fx",                     "LeveLines",             false, 110'000,  9'500 },
        { "Levels.fx",                        "Levels",                false, 110'000,  9'500 },
        { "LiftGammaGain.fx",                 "Lift / gamma / gain",   true,  195'000, 14'500 },
        { "LightDoF.fx",                      "Light DOF",             false, 980'000, 52'000 },
        { "LumaSharpen.fx",                   "LumaSharpen",           false, 180'000, 14'000 },
        { "LUT.fx",                           "LUT",                   false, 130'000, 11'000 },
        { "MagicBloom.fx",                    "MagicBloom",            false, 660'000, 52'000 },
        { "MagicDOF.fx",                      "MagicDOF",              false,1'050'000, 55'000 },
        { "MagicHDR.fx",                      "MagicHDR",              false, 380'000, 28'000 },
        { "MartyMcFly/MartyDOF.fx",           "Marty DOF",             false,1'620'000, 78'000 },
        { "MatsoDOF.fx",                      "Matso DOF",             false,1'440'000, 71'000 },
        { "MonoBleed.fx",                     "Mono bleed",            false, 105'000,  9'000 },
        { "Monochrome.fx",                    "Monochrome",            false,  85'000,  7'200 },
        { "MotionBlur.fx",                    "Motion blur",           false, 480'000, 33'000 },
        { "MotionFocus.fx",                   "Motion focus",          false, 230'000, 17'500 },
        { "MultiLUT.fx",                      "Multi-LUT",             false, 145'000, 12'000 },
        { "MXAO.fx",                          "MXAO",                  false,2'050'000,110'000 },
        { "NeoBloom.fx",                      "NeoBloom",              false, 740'000, 56'000 },
        { "NightVision.fx",                   "Night vision",          false, 145'000, 11'500 },
        { "Nostalgia.fx",                     "Nostalgia",             false, 120'000, 10'000 },
        { "OldTV.fx",                         "Old TV",                false, 180'000, 14'000 },
        { "PandaFX.fx",                       "PandaFX",               false, 280'000, 21'000 },
        { "PerfectPerspective.fx",            "Perfect perspective",   false, 175'000, 13'500 },
        { "ppfx_bloom.fx",                    "PPFX Bloom",            false, 720'000, 55'000 },
        { "ppfx_godrays.fx",                  "PPFX GodRays",          false, 940'000, 70'000 },
        { "ppfx_ssdo.fx",                     "PPFX SSDO",             false,1'180'000, 82'000 },
        { "qUINT/qUINT_bloom.fx",             "qUINT Bloom",           false, 880'000, 65'000 },
        { "qUINT/qUINT_bloomie.fx",           "qUINT BloomMie",        false, 860'000, 64'000 },
        { "qUINT/qUINT_ctgi.fx",              "qUINT CTGI",            false,3'600'000,160'000 },
        { "qUINT/qUINT_lightroom.fx",         "qUINT Lightroom",       false, 320'000, 24'000 },
        { "qUINT/qUINT_motionvectors.fx",     "Motion vectors",        false, 410'000, 31'000 },
        { "qUINT/qUINT_mxao.fx",              "MXAO (qUINT)",          false,1'650'000, 90'000 },
        { "qUINT/qUINT_screen_space_shadows.fx", "Contact shadows",    true,  760'000, 60'000 },
        { "qUINT/qUINT_sharp.fx",             "qSharp",                false, 200'000, 15'500 },
        { "qUINT/qUINT_smaa.fx",              "SMAA (qUINT)",          false, 590'000, 43'000 },
        { "qUINT/qUINT_ssao.fx",              "SSAO",                  true, 1'780'000, 95'000 },
        { "qUINT/qUINT_ssr.fx",               "SSR",                   true, 1'420'000, 88'000 },
        { "qUINT/qUINT_surface_blur.fx",      "Surface blur",          false, 380'000, 28'000 },
        { "MartyMcFly/RTGI.fx",               "RTGI",                  false,4'200'000,180'000 },
        { "ReflectiveBumpMapping.fx",         "Reflective bump",       false, 540'000, 41'000 },
        { "ReGrade.fx",                       "ReGrade",               false, 210'000, 16'000 },
        { "ReVividity.fx",                    "ReVividity",            false, 140'000, 11'500 },
        { "RingDOF.fx",                       "Ring DOF",              false,1'200'000, 60'000 },
        { "SepiaPro.fx",                      "Sepia",                 false,  90'000,  8'000 },
        { "SimpleBloom.fx",                   "Simple bloom",          false, 480'000, 38'000 },
        { "SMAA.fx",                          "SMAA",                  false, 580'000, 42'000 },
        { "CMAA_2.fx",                        "CMAA 2",                false, 520'000, 38'000 },
        { "SmartSharp.fx",                    "Smart sharp",           false, 220'000, 16'500 },
        { "StageDepth.fx",                    "Stage depth",           false,  80'000,  7'000 },
        { "SurfaceBlur.fx",                   "Surface blur",          false, 360'000, 27'000 },
        { "Technicolor.fx",                   "Technicolor",           false, 150'000, 12'000 },
        { "Technicolor2.fx",                  "Technicolor 2",         false, 160'000, 12'500 },
        { "TimeRewinder.fx",                  "Time rewinder",         false, 110'000,  9'500 },
        { "Tint.fx",                          "Tint",                  false,  90'000,  7'500 },
        { "Tonemap.fx",                       "Tonemap",               true,  280'000, 22'000 },
        { "UIDetect.fx",                      "UIDetect",              false, 100'000,  9'000 },
        { "UIMask.fx",                        "UIMask",                false,  85'000,  7'500 },
        { "Vibrance.fx",                      "Vibrance",              true,   90'000,  7'500 },
        { "Vignette.fx",                      "Vignette",              false,  95'000, 12'000 },
        { "Wireframe.fx",                     "Wireframe",             false, 200'000, 15'000 },
    };
    for (const E &e : table)
        add_tech(P(e.file), e.name, e.on, e.gpu_ns, e.cpu_ns);

    // ── Detailed uniforms for a handful of marquee effects ────────────
    add_float(P("qUINT/qUINT_ssao.fx"),  "SSAO_AMOUNT",    "Intensity",      0.55f, 0.0f, 2.0f,  "GENERAL", "Strength of the AO darkening.");
    add_float(P("qUINT/qUINT_ssao.fx"),  "SSAO_RADIUS",    "Sample radius",  0.18f, 0.05f, 1.0f, "GENERAL");
    add_int  (P("qUINT/qUINT_ssao.fx"),  "SSAO_QUALITY",   "Quality preset", 3, 0, 4,             "QUALITY");
    add_combo(P("qUINT/qUINT_ssao.fx"),  "SSAO_DEBUG",     "Debug view",     0,
              "Off\\0AO only\\0Depth\\0Normals\\0",                                              "DEBUG");
    add_bool (P("qUINT/qUINT_ssao.fx"),  "SSAO_FILTER",    "Bilateral filter", true,             "QUALITY");

    add_float(P("MXAO.fx"), "fMXAOAmbientOcclusionAmount", "AO amount",      1.20f, 0.0f, 4.0f, "GENERAL");
    add_float(P("MXAO.fx"), "fMXAOSampleRadius",            "Sample radius", 1.00f, 0.5f, 4.0f, "GENERAL");
    add_int  (P("MXAO.fx"), "iMXAOSampleCount",             "Samples",       16, 8, 64,         "QUALITY");
    add_bool (P("MXAO.fx"), "bMXAOSmoothNormals",           "Smooth normals", true,             "QUALITY");

    add_float(P("qUINT/qUINT_screen_space_shadows.fx"), "ContactShadowIntensity", "Intensity", 0.8f, 0.0f, 2.0f, "GENERAL");
    add_float(P("qUINT/qUINT_screen_space_shadows.fx"), "ContactShadowRange",     "Range",     0.5f, 0.05f, 2.0f, "GENERAL");

    add_float(P("MartyMcFly/RTGI.fx"), "RT_RAYS_AMOUNT", "Ray count", 8.0f, 1.0f, 32.0f, "RT");
    add_float(P("MartyMcFly/RTGI.fx"), "RT_RAYS_STEP",   "Step size", 0.5f, 0.05f, 2.0f, "RT");
    add_float(P("MartyMcFly/RTGI.fx"), "RT_HIT_AMOUNT",  "Hit weight",1.0f, 0.0f, 4.0f,  "RT");
    add_int  (P("MartyMcFly/RTGI.fx"), "RT_DEBUG",       "Debug view",0, 0, 6,          "DEBUG");

    add_float(P("qUINT/qUINT_ssr.fx"), "fSSR_Intensity", "Reflection strength", 0.8f, 0.0f, 1.5f, "SSR");
    add_float(P("qUINT/qUINT_ssr.fx"), "fSSR_Roughness", "Roughness",           0.6f, 0.0f, 1.0f, "SSR");

    add_float(P("Bloom.fx"), "BloomThreshold", "Threshold",  0.85f, 0.0f, 2.0f, "BLOOM");
    add_float(P("Bloom.fx"), "BloomIntensity", "Intensity",  0.45f, 0.0f, 2.0f, "BLOOM");
    add_color(P("Bloom.fx"), "BloomTint",      "Tint",       1.00f, 0.96f, 0.92f, "BLOOM");
    add_float(P("Bloom.fx"), "LensFlareAmount","Lens flare", 0.10f, 0.0f, 1.0f, "LENS");

    add_float(P("FXAA.fx"), "Subpix",        "Subpixel quality", 0.75f, 0.0f, 1.0f,  "AA");
    add_float(P("FXAA.fx"), "EdgeThreshold", "Edge threshold",   0.125f, 0.001f, 0.5f, "AA");

    add_float(P("ContrastAdaptiveSharpen.fx"), "Contrast", "Sharpness", 0.6f, 0.0f, 1.0f, "SHARPEN");

    add_combo(P("Tonemap.fx"), "TonemapOperator", "Operator", 2,
              "Linear\\0Reinhard\\0ACES\\0Filmic\\0", "TONEMAP");
    add_float(P("Tonemap.fx"), "Exposure",   "Exposure",   0.0f,  -2.0f, 2.0f, "TONEMAP");
    add_float(P("Tonemap.fx"), "Saturation", "Saturation", 1.05f, 0.0f, 2.0f,  "TONEMAP");

    add_color(P("LiftGammaGain.fx"), "RGB_Lift",  "Lift",  1.0f, 1.0f, 1.0f, "LGG");
    add_color(P("LiftGammaGain.fx"), "RGB_Gamma", "Gamma", 1.0f, 1.0f, 1.0f, "LGG");
    add_color(P("LiftGammaGain.fx"), "RGB_Gain",  "Gain",  1.0f, 1.0f, 1.0f, "LGG");

    add_float(P("CinematicDOF.fx"), "FocalLength",      "Focal length (mm)", 50.0f, 12.0f, 200.0f, "LENS");
    add_float(P("CinematicDOF.fx"), "FNumber",          "F-stop",            5.6f, 1.0f, 22.0f,    "LENS");
    add_float(P("CinematicDOF.fx"), "ManualFocusDepth", "Focus depth",       0.5f, 0.0f, 1.0f,     "FOCUS");
    add_bool (P("CinematicDOF.fx"), "UseAutoFocus",     "Auto-focus",        false,                "FOCUS");
    add_combo(P("CinematicDOF.fx"), "BokehShape",       "Bokeh shape",       2,
              "Circular\\0Hexagonal\\0Octagonal\\0Anamorphic\\0", "BOKEH");

    add_float(P("Vignette.fx"), "VignetteAmount", "Amount", 0.50f, 0.0f, 1.0f, "VIGNETTE");
    add_float(P("Vignette.fx"), "VignetteRadius", "Radius", 1.20f, 0.5f, 2.0f, "VIGNETTE");

    add_float(P("FilmGrain.fx"), "GrainIntensity", "Intensity", 0.18f, 0.0f, 1.0f, "GRAIN");
    add_float(P("FilmGrain.fx"), "GrainSize",      "Size",      1.20f, 0.5f, 3.0f, "GRAIN");

    add_float(P("Vibrance.fx"), "Vibrance", "Vibrance", 0.15f, -1.0f, 1.0f, "GRADE");

    add_float(P("Deband.fx"), "Threshold", "Threshold", 0.004f, 0.0f, 0.05f, "DEBAND");
    add_int  (P("Deband.fx"), "Range",     "Range",     16, 1, 64,            "DEBAND");
    add_int  (P("Deband.fx"), "Iterations","Iterations",1, 1, 4,              "DEBAND");

    add_int  (P("MagicHDR.fx"), "uiIntensity", "Intensity", 0, -50, 50, "HDR");
    add_int  (P("MagicHDR.fx"), "uiSaturation","Saturation",0, -100, 100,"HDR");

    add_float(P("CRT.fx"), "Curvature",     "Curvature",      0.05f, 0.0f, 0.4f, "CRT");
    add_float(P("CRT.fx"), "Scanlines",     "Scanlines",      0.5f, 0.0f, 1.0f, "CRT");
    add_float(P("CRT.fx"), "Mask",          "Mask",           0.25f, 0.0f, 1.0f, "CRT");

    add_float(P("ChromaticAberration.fx"), "Shift", "Shift",  0.5f, -10.0f, 10.0f, "CA");

    add_int  (P("PerfectPerspective.fx"), "FOV",      "FOV (deg)", 90, 30, 170, "PERSPECTIVE");
    add_combo(P("PerfectPerspective.fx"), "Type",    "Type",       0,
              "Stereographic\\0Equidistant\\0Equisolid\\0Orthographic\\0", "PERSPECTIVE");

    add_float(P("AmbientLight.fx"), "alAdaptBaseMult","Adaptive base", 1.0f, 0.0f, 2.0f, "GENERAL");
    add_float(P("AmbientLight.fx"), "alInt",          "Intensity",    1.0f, 0.0f, 5.0f, "GENERAL");

    add_float(P("Clarity.fx"),    "ClarityRadius",   "Radius",     2.0f, 0.5f, 8.0f, "CLARITY");
    add_float(P("Clarity.fx"),    "ClarityOffset",   "Offset",     2.0f, 0.5f, 8.0f, "CLARITY");
    add_float(P("Clarity.fx"),    "ClarityDarkIntensity","Darks",  0.4f, 0.0f, 2.0f, "CLARITY");
    add_float(P("Clarity.fx"),    "ClarityLightIntensity","Lights",0.0f, 0.0f, 2.0f, "CLARITY");

    add_float(P("LumaSharpen.fx"), "sharp_strength","Strength",    0.65f, 0.1f, 3.0f, "SHARPEN");
    add_float(P("LumaSharpen.fx"), "sharp_clamp",   "Clamp",       0.035f, 0.0f, 1.0f, "SHARPEN");
}

// ── Lookups ────────────────────────────────────────────────────────────────
MockTechnique *find_tech(uint64_t id)
{
    for (auto &t : state().techs) if (t.id == id) return &t;
    return nullptr;
}
MockUniform *find_uniform(uint64_t id)
{
    for (auto &u : state().uniforms) if (u.id == id) return &u;
    return nullptr;
}

void copy_to_caller(const std::string &src, char *out, size_t *cap)
{
    if (out == nullptr || cap == nullptr) return;
    const size_t want = src.size() + 1;
    const size_t can  = *cap == 0 ? 0 : (want < *cap ? want : *cap);
    if (can > 0)
    {
        std::memcpy(out, src.c_str(), can - 1);
        out[can - 1] = 0;
    }
    *cap = src.size();
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// effect_runtime — per-handle accessors
// ═══════════════════════════════════════════════════════════════════════════
namespace reshade::api {

void effect_runtime::get_technique_name(effect_technique t, char *out, size_t *cap)
{
    seed_state();
    if (auto *m = find_tech(t.handle)) copy_to_caller(m->name, out, cap);
    else if (out && cap && *cap) out[0] = 0;
}

void effect_runtime::get_technique_effect_name(effect_technique t, char *out, size_t *cap)
{
    seed_state();
    if (auto *m = find_tech(t.handle)) copy_to_caller(m->effect_full, out, cap);
    else if (out && cap && *cap) out[0] = 0;
}

bool effect_runtime::get_technique_state(effect_technique t)
{
    seed_state();
    auto *m = find_tech(t.handle);
    return m && m->enabled;
}

bool effect_runtime::get_annotation_bool_from_technique(effect_technique t, const char *name, bool *out, size_t count)
{
    seed_state();
    if (auto *m = find_tech(t.handle))
    {
        if (std::strcmp(name, "hidden") == 0)
        {
            for (size_t i = 0; i < count; ++i) out[i] = m->hidden;
            return true;
        }
    }
    return false;
}

bool effect_runtime::get_annotation_string_from_technique(effect_technique, const char *, char *out, size_t *cap)
{
    if (out && cap && *cap) out[0] = 0;
    if (cap) *cap = 0;
    return false;
}

void effect_runtime::get_uniform_variable_name(effect_uniform_variable u, char *out, size_t *cap)
{
    if (auto *m = find_uniform(u.handle)) copy_to_caller(m->name, out, cap);
    else if (out && cap && *cap) out[0] = 0;
}

void effect_runtime::get_uniform_variable_type(effect_uniform_variable u, format *base, uint32_t *rows, uint32_t *cols, uint32_t *arr)
{
    if (auto *m = find_uniform(u.handle))
    {
        if (base) *base = m->base_type;
        if (rows) *rows = m->rows;
        if (cols) *cols = m->cols;
        if (arr)  *arr  = m->arr;
    }
    else
    {
        if (base) *base = format::unknown;
        if (rows) *rows = 0;
        if (cols) *cols = 0;
        if (arr)  *arr  = 0;
    }
}

bool effect_runtime::get_annotation_string_from_uniform_variable(effect_uniform_variable u, const char *name, char *out, size_t *cap)
{
    if (auto *m = find_uniform(u.handle))
    {
        const std::string *src = nullptr;
        if      (std::strcmp(name, "ui_label")    == 0) src = &m->label;
        else if (std::strcmp(name, "ui_tooltip")  == 0) src = &m->tooltip;
        else if (std::strcmp(name, "ui_category") == 0) src = &m->category;
        else if (std::strcmp(name, "ui_type")     == 0) src = &m->ui_type;
        else if (std::strcmp(name, "ui_items")    == 0) src = &m->items;
        if (src && !src->empty())
        {
            copy_to_caller(*src, out, cap);
            return true;
        }
    }
    if (out && cap && *cap) out[0] = 0;
    if (cap) *cap = 0;
    return false;
}

bool effect_runtime::get_annotation_float_from_uniform_variable(effect_uniform_variable u, const char *name, float *out, size_t count)
{
    if (auto *m = find_uniform(u.handle))
    {
        const float *src = nullptr;
        if      (std::strcmp(name, "ui_min")  == 0 && m->has_min) src = m->ui_min;
        else if (std::strcmp(name, "ui_max")  == 0 && m->has_max) src = m->ui_max;
        else if (std::strcmp(name, "ui_step") == 0)               src = m->ui_step;
        if (src)
        {
            for (size_t i = 0; i < count; ++i) out[i] = src[i < 4 ? i : 3];
            return true;
        }
    }
    return false;
}

bool effect_runtime::get_annotation_int_from_uniform_variable(effect_uniform_variable u, const char *name, int32_t *out, size_t count)
{
    float tmp[4] = {};
    if (!get_annotation_float_from_uniform_variable(u, name, tmp, count)) return false;
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<int32_t>(tmp[i]);
    return true;
}

void effect_runtime::get_uniform_value_bool(effect_uniform_variable u, bool *out, size_t count, size_t)
{
    if (auto *m = find_uniform(u.handle))
        for (size_t i = 0; i < count; ++i) out[i] = m->bval;
    else
        for (size_t i = 0; i < count; ++i) out[i] = false;
}

void effect_runtime::get_uniform_value_float(effect_uniform_variable u, float *out, size_t count, size_t)
{
    if (auto *m = find_uniform(u.handle))
        for (size_t i = 0; i < count; ++i) out[i] = m->fval[i < 4 ? i : 3];
    else
        for (size_t i = 0; i < count; ++i) out[i] = 0.0f;
}

void effect_runtime::get_uniform_value_int(effect_uniform_variable u, int32_t *out, size_t count, size_t)
{
    if (auto *m = find_uniform(u.handle))
        for (size_t i = 0; i < count; ++i) out[i] = m->ival[i < 4 ? i : 3];
    else
        for (size_t i = 0; i < count; ++i) out[i] = 0;
}

void effect_runtime::get_uniform_value_uint(effect_uniform_variable u, uint32_t *out, size_t count, size_t)
{
    if (auto *m = find_uniform(u.handle))
        for (size_t i = 0; i < count; ++i) out[i] = m->uval[i < 4 ? i : 3];
    else
        for (size_t i = 0; i < count; ++i) out[i] = 0;
}

void effect_runtime::set_uniform_value_bool(effect_uniform_variable u, const bool *in, size_t, size_t)
{
    if (auto *m = find_uniform(u.handle)) m->bval = in[0];
}

void effect_runtime::set_uniform_value_float(effect_uniform_variable u, const float *in, size_t count, size_t)
{
    if (auto *m = find_uniform(u.handle))
        for (size_t i = 0; i < count && i < 4; ++i) m->fval[i] = in[i];
}

void effect_runtime::set_uniform_value_int(effect_uniform_variable u, const int32_t *in, size_t count, size_t)
{
    if (auto *m = find_uniform(u.handle))
        for (size_t i = 0; i < count && i < 4; ++i) m->ival[i] = in[i];
}

void effect_runtime::set_uniform_value_uint(effect_uniform_variable u, const uint32_t *in, size_t count, size_t)
{
    if (auto *m = find_uniform(u.handle))
        for (size_t i = 0; i < count && i < 4; ++i) m->uval[i] = in[i];
}

} // namespace reshade::api

// ═══════════════════════════════════════════════════════════════════════════
// runtime — top-level methods
// ═══════════════════════════════════════════════════════════════════════════
namespace reshade {

runtime::runtime() { seed_state(); }

void runtime::enumerate_techniques(const char *effect_name, api::effect_technique_callback cb, void *user_data)
{
    seed_state();
    for (auto &t : state().techs)
    {
        if (effect_name && *effect_name && t.effect_full != effect_name) continue;
        api::effect_technique h{ t.id };
        cb(this, h, user_data);
    }
}

void runtime::enumerate_uniform_variables(const char *effect_name, api::effect_uniform_callback cb, void *user_data)
{
    seed_state();
    for (auto &u : state().uniforms)
    {
        if (effect_name && *effect_name && u.effect_full != effect_name) continue;
        api::effect_uniform_variable h{ u.id };
        cb(this, h, user_data);
    }
}

void runtime::set_technique_state(api::effect_technique t, bool enabled)
{
    if (auto *m = find_tech(t.handle)) m->enabled = enabled;
}

void runtime::reset_uniform_value(api::effect_uniform_variable u)
{
    auto *m = find_uniform(u.handle); if (!m) return;
    std::memcpy(m->fval, m->default_fval, sizeof(m->fval));
    std::memcpy(m->ival, m->default_ival, sizeof(m->ival));
    std::memcpy(m->uval, m->default_uval, sizeof(m->uval));
    m->bval = m->default_bval;
}

void runtime::get_current_preset_path(char *out, size_t *cap)
{
    if (!out || !cap) return;
    const size_t want = std::strlen(state().preset_path) + 1;
    const size_t can  = *cap == 0 ? 0 : (want < *cap ? want : *cap);
    if (can > 0)
    {
        std::memcpy(out, state().preset_path, can - 1);
        out[can - 1] = 0;
    }
    *cap = want - 1;
}

void runtime::set_current_preset_path(const char *path)
{
    if (!path) return;
    std::strncpy(state().preset_path, path, sizeof(state().preset_path) - 1);
    state().preset_path[sizeof(state().preset_path) - 1] = 0;
}

void runtime::save_current_preset() { /* no-op: mock state already in memory */ }

void runtime::export_current_preset(const char *path) const
{
    // Touch the file so the picker sees it on the next refresh.
    if (!path) return;
    if (FILE *f = std::fopen(path, "wb")) std::fclose(f);
}

bool runtime::get_effects_state()           { return state().effects_on; }
void runtime::set_effects_state(bool v)     { state().effects_on = v; }

bool runtime::mariusfx_get_performance_mode() const { return state().performance_mode; }
void runtime::mariusfx_set_performance_mode(bool v) { state().performance_mode = v; }

void runtime::mariusfx_reload_all() { /* no-op in preview */ }

void runtime::mariusfx_get_technique_timing(api::effect_technique t, uint64_t *cpu_ns, uint64_t *gpu_ns) const
{
    auto *m = find_tech(t.handle);
    if (!m || !m->enabled) { if (cpu_ns) *cpu_ns = 0; if (gpu_ns) *gpu_ns = 0; return; }

    // Animate slightly so the Statistics graph + bars feel alive.
    const double t_now = state().anim_t;
    const double wob_g = 0.85 + 0.15 * std::sin(t_now * 1.3 + (double)m->id * 0.001);
    const double wob_c = 0.85 + 0.15 * std::sin(t_now * 1.7 + (double)m->id * 0.002);
    if (gpu_ns) *gpu_ns = static_cast<uint64_t>(m->gpu_ns_base * wob_g);
    if (cpu_ns) *cpu_ns = static_cast<uint64_t>(m->cpu_ns_base * wob_c);
}

// Embedded panels — display a styled "preview placeholder" card so the
// other tabs are still navigable.
static void placeholder_card(const char *title)
{
    ImGui::Dummy(ImVec2(0, 16));
    ImGui::Indent(24);
    ImGui::TextColored(ImVec4(0.95f, 0.97f, 1.00f, 1.0f), "%s (preview)", title);
    ImGui::TextColored(ImVec4(0.55f, 0.62f, 0.78f, 1.0f),
        "This panel is a thin wrapper around ReShade's internal\n"
        "draw_gui_%s() — not available in the standalone preview.\n"
        "Tweak ui.cpp/theme.cpp and rebuild — Shaders + Statistics are live.",
        title);
    ImGui::Unindent(24);
}

void runtime::draw_gui_settings()   { placeholder_card("settings"); }
void runtime::draw_gui_statistics() { placeholder_card("statistics"); }
void runtime::draw_gui_log()        { placeholder_card("log"); }
void runtime::draw_gui_addons()     { placeholder_card("addons"); }

} // namespace reshade

// ═══════════════════════════════════════════════════════════════════════════
// Public hooks for the preview main loop.
// ═══════════════════════════════════════════════════════════════════════════
namespace mariusfx_preview {

void tick_anim(float dt) { state().anim_t += dt; }

} // namespace mariusfx_preview
