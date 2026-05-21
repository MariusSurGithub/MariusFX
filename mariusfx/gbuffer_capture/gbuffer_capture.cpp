/*
 * MariusFX — RAGE GBuffer Capture (implementation)
 * See gbuffer_capture.hpp for architecture overview.
 */

#include "gbuffer_capture.hpp"
#include "../effects/ssao_injector.hpp"
#include "../transpiler/pipeline_scheduler.hpp"
#include "../../source/runtime.hpp"
#include "../../source/dll_log.hpp"
#include <atomic>
#include <mutex>
#include <cstring>

namespace mariusfx::gbuffer_capture {

// ─── Internal state ──────────────────────────────────────────────────────────

static std::atomic<bool> g_enabled{true};

// Backbuffer dimensions (set from runtime at init/resize).
static UINT g_bb_width  = 0;
static UINT g_bb_height = 0;

// Per-frame tracking
static std::atomic<int> g_mrt_bindings_this_frame{0};
static bool g_gbuffer_pass_active  = false;
static int  g_gbuffer_draw_count   = 0;
static bool g_captured_this_frame  = false;

// Minimum draws during a GBuffer-like MRT binding to consider it valid.
// RAGE's GBuffer fill pass has thousands of draws; we set a low threshold
// to avoid false positives from brief 4-MRT bindings (e.g. particle passes).
static constexpr int kMinDrawsForCapture = 50;

// MRT slot mapping detected during the GBuffer pass.
// We identify buffers by DXGI format:
//   - R16G16B16A16_FLOAT      → HDR (slot 0 usually)
//   - R8G8B8A8_UNORM/_SRGB    → Albedo or Specular (multiple; order matters)
//   - R10G10B10A2_UNORM       → Normals (octahedral encoded)
//   - R16G16_FLOAT            → Motion vectors
//   - R8G8B8A8_SNORM          → Normals (alt encoding, some builds)

enum GBufSlot : int {
    GBUF_HDR      = 0,
    GBUF_ALBEDO   = 1,
    GBUF_NORMAL   = 2,
    GBUF_SPECULAR = 3,
    GBUF_MOTION   = 4,
    GBUF_COUNT    = 5
};

static const char *kSemantics[GBUF_COUNT] = {
    "RAGEGBufferHDR",
    "RAGEGBufferAlbedo",
    "RAGEGBufferNormal",
    "RAGEGBufferSpecular",
    "RAGEGBufferMotion"
};

// Captured resources — these are COPIES we own.
static ID3D11Texture2D          *g_copy_tex[GBUF_COUNT] = {};
static ID3D11ShaderResourceView *g_copy_srv[GBUF_COUNT] = {};

// Source resources detected during the GBuffer pass.
static ID3D11Resource *g_source_res[GBUF_COUNT] = {};

// SSAO injection resources (UAV for HDR buffer, SRV for depth)
static ID3D11Texture2D           *g_hdr_uav_tex = nullptr;
static ID3D11UnorderedAccessView *g_hdr_uav = nullptr;
static ID3D11ShaderResourceView  *g_depth_srv = nullptr;

// DSV captured DURING the GBuffer pass (Case 1). This is the depth buffer
// that RAGE writes geometry depth to. We need to keep an AddRef on it so it
// stays alive even after RAGE unbinds it.
static ID3D11DepthStencilView    *g_gbuffer_dsv = nullptr;

// Debug output texture (when debug viz mode active, AO/Normal/Depth is written here
// and copied to backbuffer at end of frame). Format = R16G16B16A16_FLOAT for HDR-safe debug.
static ID3D11Texture2D           *g_debug_tex = nullptr;
static ID3D11UnorderedAccessView *g_debug_uav = nullptr;
static ID3D11ShaderResourceView  *g_debug_srv = nullptr;

// Debug visualization mode (0=off, 1=Normal, 2=Depth, 3=AO, 4=GBufferRaw)
static std::atomic<int> g_debug_viz_mode{0};

// Mutex protecting resource creation/destruction.
static std::mutex g_resource_mutex;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static DXGI_FORMAT get_rtv_format(ID3D11RenderTargetView *rtv)
{
    D3D11_RENDER_TARGET_VIEW_DESC desc;
    rtv->GetDesc(&desc);
    return desc.Format;
}

static bool get_texture_size(ID3D11Resource *res, UINT &w, UINT &h, DXGI_FORMAT &fmt)
{
    ID3D11Texture2D *tex = nullptr;
    if (FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tex)))
        return false;
    D3D11_TEXTURE2D_DESC d;
    tex->GetDesc(&d);
    w = d.Width;
    h = d.Height;
    fmt = d.Format;
    tex->Release();
    return true;
}

// Classifies a DXGI format into a GBuffer slot. Returns -1 if unknown.
static int classify_format(DXGI_FORMAT fmt, int r8_index)
{
    switch (fmt)
    {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
        return GBUF_HDR;

    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
        return GBUF_NORMAL;

    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_SNORM:
        return GBUF_MOTION;

    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        // Multiple R8G8B8A8 slots: first = albedo, second = specular
        return (r8_index == 0) ? GBUF_ALBEDO : GBUF_SPECULAR;

    default:
        return -1;
    }
}

// Ensures we have a copy texture + SRV matching the source format/size.
static bool ensure_copy_resources(ID3D11Device *device, int slot, UINT w, UINT h, DXGI_FORMAT fmt)
{
    if (g_copy_tex[slot])
    {
        D3D11_TEXTURE2D_DESC existing;
        g_copy_tex[slot]->GetDesc(&existing);
        if (existing.Width == w && existing.Height == h && existing.Format == fmt)
            return true; // Already valid.
        // Size/format changed — recreate.
        g_copy_srv[slot]->Release(); g_copy_srv[slot] = nullptr;
        g_copy_tex[slot]->Release(); g_copy_tex[slot] = nullptr;
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width      = w;
    td.Height     = h;
    td.MipLevels  = 1;
    td.ArraySize  = 1;
    td.Format     = fmt;
    td.SampleDesc = {1, 0};
    td.Usage      = D3D11_USAGE_DEFAULT;
    td.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(device->CreateTexture2D(&td, nullptr, &g_copy_tex[slot])))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format              = fmt;
    srvd.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;

    if (FAILED(device->CreateShaderResourceView(g_copy_tex[slot], &srvd, &g_copy_srv[slot])))
    {
        g_copy_tex[slot]->Release(); g_copy_tex[slot] = nullptr;
        return false;
    }

    return true;
}

// Check that a DXGI format supports UAV access on this device.
// Per D3D11 docs, only a subset of formats are guaranteed UAV-loadable.
// R10G10B10A2_UNORM is NOT UAV-loadable without feature level 11.1 + format support check.
static bool format_supports_uav(ID3D11Device *device, DXGI_FORMAT fmt)
{
    UINT support = 0;
    if (FAILED(device->CheckFormatSupport(fmt, &support)))
        return false;
    return (support & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0;
}

// Create the UAV-enabled HDR copy texture.
// Returns true if g_hdr_uav_tex / g_hdr_uav are valid afterwards.
static bool ensure_hdr_uav(ID3D11Device *device)
{
    if (!g_source_res[GBUF_HDR])
        return false;

    UINT w, h; DXGI_FORMAT fmt;
    if (!get_texture_size(g_source_res[GBUF_HDR], w, h, fmt))
        return false;

    // Resolve typeless to concrete (HDR is typically R16G16B16A16_TYPELESS -> _FLOAT).
    if (fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS) fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (fmt == DXGI_FORMAT_R10G10B10A2_TYPELESS)  fmt = DXGI_FORMAT_R10G10B10A2_UNORM;
    if (fmt == DXGI_FORMAT_R8G8B8A8_TYPELESS)     fmt = DXGI_FORMAT_R8G8B8A8_UNORM;

    // Reuse existing if size/format match.
    if (g_hdr_uav_tex)
    {
        D3D11_TEXTURE2D_DESC existing;
        g_hdr_uav_tex->GetDesc(&existing);
        if (existing.Width == w && existing.Height == h && existing.Format == fmt)
            return g_hdr_uav != nullptr;
        // Otherwise recreate
        if (g_hdr_uav)     { g_hdr_uav->Release();     g_hdr_uav     = nullptr; }
        if (g_hdr_uav_tex) { g_hdr_uav_tex->Release(); g_hdr_uav_tex = nullptr; }
    }

    // Verify UAV support for this format before creating.
    if (!format_supports_uav(device, fmt))
    {
        reshade::log::message(reshade::log::level::warning,
            "[MariusFX GBuffer] HDR format 0x%X does not support UAV — SSAO injection disabled", fmt);
        return false;
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width      = w;
    td.Height     = h;
    td.MipLevels  = 1;
    td.ArraySize  = 1;
    td.Format     = fmt;
    td.SampleDesc = {1, 0};
    td.Usage      = D3D11_USAGE_DEFAULT;
    td.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

    HRESULT hr = device->CreateTexture2D(&td, nullptr, &g_hdr_uav_tex);
    if (FAILED(hr))
    {
        reshade::log::message(reshade::log::level::error,
            "[MariusFX GBuffer] CreateTexture2D(HDR UAV) failed 0x%08X (fmt 0x%X %ux%u)", hr, fmt, w, h);
        return false;
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = fmt;
    ud.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    ud.Texture2D.MipSlice = 0;

    hr = device->CreateUnorderedAccessView(g_hdr_uav_tex, &ud, &g_hdr_uav);
    if (FAILED(hr))
    {
        reshade::log::message(reshade::log::level::error,
            "[MariusFX GBuffer] CreateUnorderedAccessView(HDR) failed 0x%08X", hr);
        g_hdr_uav_tex->Release();
        g_hdr_uav_tex = nullptr;
        return false;
    }

    reshade::log::message(reshade::log::level::info,
        "[MariusFX GBuffer] HDR UAV ready (%ux%u fmt 0x%X)", w, h, fmt);
    return true;
}

// Create the depth SRV from the captured GBuffer DSV.
// IMPORTANT: uses g_gbuffer_dsv (captured during Case 1), NOT the current dsv parameter.
static bool ensure_depth_srv(ID3D11Device *device)
{
    if (g_depth_srv)
        return true; // already valid

    if (!g_gbuffer_dsv)
    {
        reshade::log::message(reshade::log::level::warning,
            "[MariusFX GBuffer] No DSV captured during GBuffer pass — cannot create depth SRV");
        return false;
    }

    ID3D11Resource *depth_res = nullptr;
    g_gbuffer_dsv->GetResource(&depth_res);
    if (!depth_res)
        return false;

    ID3D11Texture2D *depth_tex = nullptr;
    depth_res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&depth_tex);
    if (!depth_tex)
    {
        depth_res->Release();
        return false;
    }

    D3D11_TEXTURE2D_DESC dd;
    depth_tex->GetDesc(&dd);
    depth_tex->Release();

    // Texture must be SRV-bindable. RAGE typically creates depth with
    // D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE; if not, we cannot read it.
    if ((dd.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0)
    {
        reshade::log::message(reshade::log::level::warning,
            "[MariusFX GBuffer] Depth texture lacks D3D11_BIND_SHADER_RESOURCE — cannot create SRV (fmt 0x%X)", dd.Format);
        depth_res->Release();
        return false;
    }

    DXGI_FORMAT srv_format = DXGI_FORMAT_UNKNOWN;
    switch (dd.Format)
    {
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
        srv_format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        break;
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:
        srv_format = DXGI_FORMAT_R32_FLOAT;
        break;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        srv_format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        break;
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_TYPELESS:
        srv_format = DXGI_FORMAT_R16_UNORM;
        break;
    default:
        reshade::log::message(reshade::log::level::warning,
            "[MariusFX GBuffer] Unknown depth tex format 0x%X — falling back to R32_FLOAT", dd.Format);
        srv_format = DXGI_FORMAT_R32_FLOAT;
        break;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = srv_format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;

    HRESULT hr = device->CreateShaderResourceView(depth_res, &sd, &g_depth_srv);
    depth_res->Release();

    if (FAILED(hr))
    {
        reshade::log::message(reshade::log::level::error,
            "[MariusFX GBuffer] CreateShaderResourceView(depth) failed 0x%08X (srv_fmt 0x%X)", hr, srv_format);
        return false;
    }

    reshade::log::message(reshade::log::level::info,
        "[MariusFX GBuffer] Depth SRV ready (tex_fmt 0x%X -> srv_fmt 0x%X)", dd.Format, srv_format);
    return true;
}

// Composite helper: ensure both HDR UAV and depth SRV are ready.
static bool ensure_ssao_resources(ID3D11Device *device)
{
    if (!ensure_hdr_uav(device))   return false;
    if (!ensure_depth_srv(device)) return false;
    return true;
}

// ─── Public API ──────────────────────────────────────────────────────────────

bool enabled() { return g_enabled.load(std::memory_order_relaxed); }
void set_enabled(bool on) { g_enabled.store(on, std::memory_order_relaxed); }
int  debug_mrt_bindings_this_frame() { return g_mrt_bindings_this_frame.load(std::memory_order_relaxed); }
void debug_get_bb_size(UINT &w, UINT &h) { w = g_bb_width; h = g_bb_height; }
void set_backbuffer_size(UINT w, UINT h)
{
	if (g_bb_width != w || g_bb_height != h)
	{
		reshade::log::message(reshade::log::level::info,
			"[MariusFX GBuffer] Backbuffer size set to %ux%u", w, h);
	}
	g_bb_width = w;
	g_bb_height = h;
}

void on_omset_rt(ID3D11DeviceContext *ctx,
                 UINT num_views,
                 ID3D11RenderTargetView *const *rtvs,
                 ID3D11DepthStencilView *dsv)
{
    if (!g_enabled.load(std::memory_order_relaxed))
        return;
    if (g_bb_width == 0 || g_bb_height == 0)
        return;

    // ── Case 1: Entering a potential GBuffer pass (>= 4 MRTs) ────────────
    if (num_views >= 4 && rtvs != nullptr && !g_gbuffer_pass_active && !g_captured_this_frame)
    {
        // Validate: all RTVs non-null, all same size matching backbuffer.
        bool valid = true;
        int r8_count = 0;
        int assigned[GBUF_COUNT];
        std::memset(assigned, -1, sizeof(assigned));
        ID3D11Resource *sources[GBUF_COUNT] = {};

        for (UINT i = 0; i < num_views && i < 8; ++i)
        {
            if (!rtvs[i]) { valid = false; break; }

            ID3D11Resource *res = nullptr;
            rtvs[i]->GetResource(&res);
            if (!res) { valid = false; break; }

            UINT w, h; DXGI_FORMAT fmt;
            if (!get_texture_size(res, w, h, fmt))
            {
                res->Release(); valid = false; break;
            }

            // Must match backbuffer dimensions (allow ±1px for rounding).
            if (w < g_bb_width - 1 || w > g_bb_width + 1 ||
                h < g_bb_height - 1 || h > g_bb_height + 1)
            {
                res->Release(); valid = false; break;
            }

            // Use RTV desc format if resource format is typeless.
            DXGI_FORMAT classify_fmt = fmt;
            if (fmt == DXGI_FORMAT_R8G8B8A8_TYPELESS || fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS ||
                fmt == DXGI_FORMAT_R10G10B10A2_TYPELESS || fmt == DXGI_FORMAT_B8G8R8A8_TYPELESS ||
                fmt == DXGI_FORMAT_R16G16_TYPELESS)
            {
                classify_fmt = get_rtv_format(rtvs[i]);
            }

            int slot = classify_format(classify_fmt, r8_count);
            if (slot >= 0 && assigned[slot] == -1)
            {
                assigned[slot] = (int)i;
                sources[slot] = res; // keep ref
            }
            else
            {
                res->Release();
            }

            if (classify_fmt == DXGI_FORMAT_R8G8B8A8_UNORM ||
                classify_fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                classify_fmt == DXGI_FORMAT_B8G8R8A8_UNORM ||
                classify_fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                ++r8_count;
        }

        // Need at least Normal + one other buffer to be worth capturing.
        if (valid && assigned[GBUF_NORMAL] != -1 && (assigned[GBUF_HDR] != -1 || assigned[GBUF_ALBEDO] != -1))
        {
            g_gbuffer_pass_active = true;
            g_gbuffer_draw_count  = 0;
            g_mrt_bindings_this_frame.fetch_add(1, std::memory_order_relaxed);
            reshade::log::message(reshade::log::level::info,
                "[MariusFX GBuffer] Detected %u-MRT pass at %ux%u (slots: HDR=%d Albedo=%d Normal=%d Spec=%d Motion=%d)",
                num_views, g_bb_width, g_bb_height,
                assigned[GBUF_HDR], assigned[GBUF_ALBEDO], assigned[GBUF_NORMAL],
                assigned[GBUF_SPECULAR], assigned[GBUF_MOTION]);

            // Store source resource pointers.
            for (int s = 0; s < GBUF_COUNT; ++s)
            {
                if (g_source_res[s]) { g_source_res[s]->Release(); g_source_res[s] = nullptr; }
                g_source_res[s] = sources[s]; // transferred ownership of AddRef from GetResource
            }

            // CRITICAL: capture the DSV used DURING the GBuffer pass. This is the
            // depth buffer RAGE is writing to right now. We must AddRef it because
            // RAGE will rebind a different DSV when the pass ends (Case 2 dsv != this dsv).
            if (g_gbuffer_dsv) { g_gbuffer_dsv->Release(); g_gbuffer_dsv = nullptr; }
            if (dsv)
            {
                dsv->AddRef();
                g_gbuffer_dsv = dsv;
            }
        }
        else
        {
            // Release any refs we took.
            for (int s = 0; s < GBUF_COUNT; ++s)
                if (sources[s]) sources[s]->Release();
        }
        return;
    }

    // ── Case 2: Exiting the GBuffer pass (MRT count drops) ───────────────
    if (g_gbuffer_pass_active && (num_views < 4 || rtvs == nullptr))
    {
        g_gbuffer_pass_active = false;

        // Only capture if enough draws happened (avoids false positive from
        // brief multi-MRT bindings like particle/water passes).
        if (g_gbuffer_draw_count >= kMinDrawsForCapture)
        {
            // Perform the copy.
            ID3D11Device *device = nullptr;
            ctx->GetDevice(&device);
            if (device)
            {
                std::lock_guard<std::mutex> lock(g_resource_mutex);
                int copied = 0;
                for (int s = 0; s < GBUF_COUNT; ++s)
                {
                    if (!g_source_res[s]) continue;

                    UINT w, h; DXGI_FORMAT fmt;
                    if (!get_texture_size(g_source_res[s], w, h, fmt)) continue;

                    // Use RTV-compatible format for the copy texture.
                    // If resource is typeless, pick a concrete format.
                    if (fmt == DXGI_FORMAT_R8G8B8A8_TYPELESS) fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
                    if (fmt == DXGI_FORMAT_R16G16B16A16_TYPELESS) fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
                    if (fmt == DXGI_FORMAT_R10G10B10A2_TYPELESS) fmt = DXGI_FORMAT_R10G10B10A2_UNORM;
                    if (fmt == DXGI_FORMAT_B8G8R8A8_TYPELESS) fmt = DXGI_FORMAT_B8G8R8A8_UNORM;
                    if (fmt == DXGI_FORMAT_R16G16_TYPELESS) fmt = DXGI_FORMAT_R16G16_FLOAT;

                    if (ensure_copy_resources(device, s, w, h, fmt))
                    {
                        ctx->CopyResource(g_copy_tex[s], g_source_res[s]);
                        copied++;
                    }
                }
                g_captured_this_frame = true;
                reshade::log::message(reshade::log::level::info,
                    "[MariusFX GBuffer] Captured %d buffers after %d draws", copied, g_gbuffer_draw_count);

                // ── OPTIONAL: INJECT SSAO PASS ──────────────────────────────
                // Gated behind ssao_injector::is_enabled() so we can ship the
                // GBuffer capture (used by .fx shaders via RAGEGBufferNormal
                // etc.) without risking HDR corruption. Default = disabled.
                //
                // When enabled, we:
                //   1) CopyResource HDR -> UAV-enabled clone (~0.3ms @ 1080p)
                //   2) Dispatch compute shader writing AO into the clone
                //   3) CopyResource clone -> HDR (~0.3ms)
                //
                // Note: uses g_gbuffer_dsv (captured during Case 1), not the
                // `dsv` parameter (which is the NEW dsv being bound now).
                if (ssao_injector::is_enabled() && ssao_injector::is_ready())
                {
                    if (ensure_ssao_resources(device))
                    {
                        ctx->CopyResource(g_hdr_uav_tex, g_source_res[GBUF_HDR]);
                        ssao_injector::inject_ssao_pass(
                            ctx,
                            g_copy_srv[GBUF_NORMAL],
                            g_depth_srv,
                            g_hdr_uav,
                            g_bb_width,
                            g_bb_height);
                        ctx->CopyResource(g_source_res[GBUF_HDR], g_hdr_uav_tex);
                    }
                }
                
                // [MariusFX Transpiler] Execute transpiled shaders at AFTER_GBUFFER injection point
                {
                    static bool scheduler_initialized = false;
                    if (!scheduler_initialized)
                    {
                        transpiler::get_scheduler().initialize(device);
                        scheduler_initialized = true;
                        reshade::log::message(reshade::log::level::info,
                            "[MariusFX Transpiler] Scheduler initialized");
                    }
                    
                    transpiler::get_scheduler().execute_at_point(
                        ctx,
                        transpiler::InjectionPoint::AFTER_GBUFFER,
                        g_bb_width,
                        g_bb_height
                    );
                }

                device->Release();
            }
        }

        // Release source refs.
        for (int s = 0; s < GBUF_COUNT; ++s)
        {
            if (g_source_res[s]) { g_source_res[s]->Release(); g_source_res[s] = nullptr; }
        }
    }
}

void on_draw(ID3D11DeviceContext * /*ctx*/)
{
    if (g_gbuffer_pass_active)
        ++g_gbuffer_draw_count;
}

void bind_to_runtime(reshade::runtime *rt)
{
    if (!g_enabled.load(std::memory_order_relaxed))
        return;
    if (!g_captured_this_frame)
        return;

    std::lock_guard<std::mutex> lock(g_resource_mutex);
    int bound = 0;
    for (int s = 0; s < GBUF_COUNT; ++s)
    {
        if (g_copy_srv[s])
        {
            // Cast ID3D11ShaderResourceView* to api::resource_view handle
            reshade::api::resource_view srv = { reinterpret_cast<uintptr_t>(g_copy_srv[s]) };
            rt->update_texture_bindings(kSemantics[s], srv, srv); // Use same SRV for both linear and sRGB
            bound++;
        }
    }
    if (bound > 0)
    {
        reshade::log::message(reshade::log::level::info,
            "[MariusFX GBuffer] Bound %d textures to runtime (Normal=%s)",
            bound, g_copy_srv[GBUF_NORMAL] ? "YES" : "NO");
    }
}

void on_present_end()
{
    g_gbuffer_pass_active    = false;
    g_gbuffer_draw_count     = 0;
    g_captured_this_frame    = false;
    g_mrt_bindings_this_frame.store(0, std::memory_order_relaxed);

    // Release any leftover source refs (safety).
    for (int s = 0; s < GBUF_COUNT; ++s)
    {
        if (g_source_res[s]) { g_source_res[s]->Release(); g_source_res[s] = nullptr; }
    }
}

void on_swapchain_invalidate()
{
    std::lock_guard<std::mutex> lock(g_resource_mutex);
    for (int s = 0; s < GBUF_COUNT; ++s)
    {
        if (g_copy_srv[s]) { g_copy_srv[s]->Release(); g_copy_srv[s] = nullptr; }
        if (g_copy_tex[s]) { g_copy_tex[s]->Release(); g_copy_tex[s] = nullptr; }
        if (g_source_res[s]) { g_source_res[s]->Release(); g_source_res[s] = nullptr; }
    }
    
    // Release SSAO injection resources
    if (g_depth_srv)    { g_depth_srv->Release();    g_depth_srv    = nullptr; }
    if (g_hdr_uav)      { g_hdr_uav->Release();      g_hdr_uav      = nullptr; }
    if (g_hdr_uav_tex)  { g_hdr_uav_tex->Release();  g_hdr_uav_tex  = nullptr; }
    if (g_gbuffer_dsv)  { g_gbuffer_dsv->Release();  g_gbuffer_dsv  = nullptr; }
    
    // Release debug viz resources
    if (g_debug_srv)    { g_debug_srv->Release();    g_debug_srv    = nullptr; }
    if (g_debug_uav)    { g_debug_uav->Release();    g_debug_uav    = nullptr; }
    if (g_debug_tex)    { g_debug_tex->Release();    g_debug_tex    = nullptr; }
    
    g_captured_this_frame = false;
    g_gbuffer_pass_active = false;
}

} // namespace mariusfx::gbuffer_capture
