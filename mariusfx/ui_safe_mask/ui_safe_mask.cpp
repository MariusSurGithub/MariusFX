/*
 * MariusFX — UI-safe post-process compositing pass implementation.
 * See ui_safe_mask.hpp for the strategy overview.
 */

#include "ui_safe_mask.hpp"

#include <atomic>
#include <mutex>
#include <cstring>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace mariusfx::ui_safe_mask {
namespace {

// ── Compositing pixel shader ────────────────────────────────────────────────

constexpr const char *kFullscreenVS =
    "struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VS_OUT VSMain(uint id : SV_VertexID) {\n"
    "    VS_OUT o;\n"
    "    o.uv  = float2((id << 1) & 2, id & 2);\n"
    "    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "    return o;\n"
    "}\n";

constexpr const char *kCompositingPS =
    "Texture2D effects_result : register(t0);\n"
    "Texture2D bb_with_ui     : register(t1);\n"
    "Texture2D scene_clean    : register(t2);\n"
    "SamplerState samp        : register(s0);\n"
    "float4 PSMain(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
    "    float4 fx   = effects_result.Sample(samp, uv);\n"
    "    float4 ui   = bb_with_ui    .Sample(samp, uv);\n"
    "    float4 snap = scene_clean   .Sample(samp, uv);\n"
    "    float diff      = length(ui.rgb - snap.rgb);\n"
    "    float ui_amount = smoothstep(0.005, 0.05, diff);\n"
    "    float3 col = lerp(fx.rgb, ui.rgb, ui_amount);\n"
    "    return float4(col, fx.a);\n"
    "}\n";

// ── Persistent state ────────────────────────────────────────────────────────

std::mutex                    g_mtx;
std::atomic<bool>             g_enabled              { true };
std::atomic<bool>             g_pipeline_ready       { false };
std::atomic<bool>             g_scene_clean_captured { false };
std::atomic<uint32_t>         g_bb_draw_count        { 0 };

// Raw pointer used for pointer-equality checks only — never dereferenced
// from this storage. Updated when on_swapchain_present_begin sees a new
// BB resource. Cleared on swapchain invalidate.
std::atomic<ID3D11Resource *> g_bb_resource_ptr      { nullptr };

// Thread-local: true when the most recent OMSetRenderTargets bound the
// swapchain BB as RT slot 0. Read by on_draw() to decide whether to
// snapshot scene_clean.
thread_local bool             tl_rt0_is_bb           = false;

// Persistent shaders + state objects (created once per device).
ComPtr<ID3D11Device>          g_device;
ComPtr<ID3D11VertexShader>    g_vs;
ComPtr<ID3D11PixelShader>     g_ps;
ComPtr<ID3D11SamplerState>    g_sampler;
ComPtr<ID3D11BlendState>      g_blend;
ComPtr<ID3D11DepthStencilState> g_dss;
ComPtr<ID3D11RasterizerState> g_raster;

// Per-resolution textures + views (recreated on resize).
DXGI_FORMAT                   g_bb_format = DXGI_FORMAT_UNKNOWN;
UINT                          g_bb_width  = 0;
UINT                          g_bb_height = 0;
ComPtr<ID3D11Texture2D>       g_scene_clean_tex;
ComPtr<ID3D11ShaderResourceView> g_scene_clean_srv;
ComPtr<ID3D11Texture2D>       g_bb_with_ui_tex;
ComPtr<ID3D11ShaderResourceView> g_bb_with_ui_srv;
ComPtr<ID3D11Texture2D>       g_effects_result_tex;
ComPtr<ID3D11ShaderResourceView> g_effects_result_srv;
ComPtr<ID3D11RenderTargetView> g_bb_rtv;

// ── Helpers ─────────────────────────────────────────────────────────────────

bool compile_shader(const char *src, const char *entry, const char *target,
                    ID3DBlob **out_blob)
{
    ComPtr<ID3DBlob> errors;
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr,
                            entry, target, flags, 0, out_blob, &errors);
    return SUCCEEDED(hr) && *out_blob != nullptr;
}

// Lazy one-time creation of device-level resources (shaders, sampler, etc.).
// Returns true if everything is ready or could be created. Called under g_mtx.
bool ensure_pipeline_locked(ID3D11Device *dev)
{
    if (g_pipeline_ready.load(std::memory_order_relaxed))
    {
        return g_device.Get() == dev;
    }

    // VS
    ComPtr<ID3DBlob> vs_blob;
    if (!compile_shader(kFullscreenVS, "VSMain", "vs_5_0", &vs_blob))
        return false;
    if (FAILED(dev->CreateVertexShader(vs_blob->GetBufferPointer(),
                                       vs_blob->GetBufferSize(), nullptr, &g_vs)))
        return false;

    // PS
    ComPtr<ID3DBlob> ps_blob;
    if (!compile_shader(kCompositingPS, "PSMain", "ps_5_0", &ps_blob))
        return false;
    if (FAILED(dev->CreatePixelShader(ps_blob->GetBufferPointer(),
                                      ps_blob->GetBufferSize(), nullptr, &g_ps)))
        return false;

    // Sampler (linear clamp).
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD         = D3D11_FLOAT32_MAX;
    if (FAILED(dev->CreateSamplerState(&sd, &g_sampler))) return false;

    // Opaque blend (we fully overwrite the BB).
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(dev->CreateBlendState(&bd, &g_blend))) return false;

    // Depth/stencil disabled.
    D3D11_DEPTH_STENCIL_DESC dsd = {};
    if (FAILED(dev->CreateDepthStencilState(&dsd, &g_dss))) return false;

    // Rasterizer: fill solid, no cull, no scissor.
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    if (FAILED(dev->CreateRasterizerState(&rd, &g_raster))) return false;

    g_device = dev;
    g_pipeline_ready.store(true, std::memory_order_release);
    return true;
}

void release_per_resolution_locked()
{
    g_scene_clean_srv.Reset();
    g_scene_clean_tex.Reset();
    g_bb_with_ui_srv.Reset();
    g_bb_with_ui_tex.Reset();
    g_effects_result_srv.Reset();
    g_effects_result_tex.Reset();
    g_bb_rtv.Reset();
    g_bb_format = DXGI_FORMAT_UNKNOWN;
    g_bb_width = g_bb_height = 0;
    g_scene_clean_captured.store(false, std::memory_order_release);
    g_bb_draw_count.store(0, std::memory_order_release);
}

bool ensure_per_resolution_locked(ID3D11Device *dev, ID3D11Resource *bb_res)
{
    ComPtr<ID3D11Texture2D> bb_tex;
    if (FAILED(bb_res->QueryInterface(IID_PPV_ARGS(&bb_tex)))) return false;

    D3D11_TEXTURE2D_DESC bbd = {};
    bb_tex->GetDesc(&bbd);

    if (g_scene_clean_tex && g_bb_with_ui_tex && g_effects_result_tex &&
        g_bb_format == bbd.Format && g_bb_width == bbd.Width && g_bb_height == bbd.Height)
    {
        return true;
    }

    release_per_resolution_locked();

    D3D11_TEXTURE2D_DESC td = bbd;
    td.MipLevels      = 1;
    td.ArraySize      = 1;
    td.SampleDesc     = {1, 0};
    td.Usage          = D3D11_USAGE_DEFAULT;
    td.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = 0;
    td.MiscFlags      = 0;

    if (FAILED(dev->CreateTexture2D(&td, nullptr, &g_scene_clean_tex)))    return false;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &g_bb_with_ui_tex)))      return false;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &g_effects_result_tex))) return false;

    if (FAILED(dev->CreateShaderResourceView(g_scene_clean_tex.Get(),    nullptr, &g_scene_clean_srv)))    return false;
    if (FAILED(dev->CreateShaderResourceView(g_bb_with_ui_tex.Get(),     nullptr, &g_bb_with_ui_srv)))     return false;
    if (FAILED(dev->CreateShaderResourceView(g_effects_result_tex.Get(), nullptr, &g_effects_result_srv))) return false;

    if (FAILED(dev->CreateRenderTargetView(bb_tex.Get(), nullptr, &g_bb_rtv))) return false;

    g_bb_format = bbd.Format;
    g_bb_width  = bbd.Width;
    g_bb_height = bbd.Height;
    return true;
}

// ── Pipeline-state save/restore ─────────────────────────────────────────────
// Our compositing pass clobbers many bindings. We capture what we touch and
// restore it after the draw so the host app (and ReShade's draw_gui) keep
// working on consistent state.

struct SavedState
{
    ComPtr<ID3D11RenderTargetView>      rtvs[8];
    ComPtr<ID3D11DepthStencilView>      dsv;
    ComPtr<ID3D11ShaderResourceView>    ps_srvs[3];
    ComPtr<ID3D11SamplerState>          ps_samplers[1];
    ComPtr<ID3D11VertexShader>          vs;
    ComPtr<ID3D11PixelShader>           ps;
    ComPtr<ID3D11InputLayout>           ia_layout;
    D3D11_PRIMITIVE_TOPOLOGY            ia_topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ComPtr<ID3D11Buffer>                ia_vb;
    UINT                                ia_vb_stride = 0;
    UINT                                ia_vb_offset = 0;
    ComPtr<ID3D11Buffer>                ia_ib;
    DXGI_FORMAT                         ia_ib_format = DXGI_FORMAT_UNKNOWN;
    UINT                                ia_ib_offset = 0;
    ComPtr<ID3D11BlendState>            blend;
    FLOAT                               blend_factor[4] = {0,0,0,0};
    UINT                                blend_sample_mask = 0xffffffffu;
    ComPtr<ID3D11DepthStencilState>     dss;
    UINT                                stencil_ref = 0;
    ComPtr<ID3D11RasterizerState>       raster;
    UINT                                num_viewports = 0;
    D3D11_VIEWPORT                      viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] {};
};

void state_save(ID3D11DeviceContext *ctx, SavedState &s)
{
    ID3D11RenderTargetView *rtvs[8] = {};
    ID3D11DepthStencilView *dsv = nullptr;
    ctx->OMGetRenderTargets(8, rtvs, &dsv);
    for (int i = 0; i < 8; ++i) s.rtvs[i].Attach(rtvs[i]);
    s.dsv.Attach(dsv);

    ID3D11ShaderResourceView *srvs[3] = {};
    ctx->PSGetShaderResources(0, 3, srvs);
    for (int i = 0; i < 3; ++i) s.ps_srvs[i].Attach(srvs[i]);

    ID3D11SamplerState *samp[1] = {};
    ctx->PSGetSamplers(0, 1, samp);
    s.ps_samplers[0].Attach(samp[0]);

    ID3D11VertexShader *vs = nullptr;
    UINT n = 0;
    ctx->VSGetShader(&vs, nullptr, &n);
    s.vs.Attach(vs);

    ID3D11PixelShader *ps = nullptr;
    n = 0;
    ctx->PSGetShader(&ps, nullptr, &n);
    s.ps.Attach(ps);

    ID3D11InputLayout *il = nullptr;
    ctx->IAGetInputLayout(&il);
    s.ia_layout.Attach(il);

    ctx->IAGetPrimitiveTopology(&s.ia_topology);

    ID3D11Buffer *vb = nullptr;
    ctx->IAGetVertexBuffers(0, 1, &vb, &s.ia_vb_stride, &s.ia_vb_offset);
    s.ia_vb.Attach(vb);

    ID3D11Buffer *ib = nullptr;
    ctx->IAGetIndexBuffer(&ib, &s.ia_ib_format, &s.ia_ib_offset);
    s.ia_ib.Attach(ib);

    ID3D11BlendState *bs = nullptr;
    ctx->OMGetBlendState(&bs, s.blend_factor, &s.blend_sample_mask);
    s.blend.Attach(bs);

    ID3D11DepthStencilState *dss = nullptr;
    ctx->OMGetDepthStencilState(&dss, &s.stencil_ref);
    s.dss.Attach(dss);

    ID3D11RasterizerState *rs = nullptr;
    ctx->RSGetState(&rs);
    s.raster.Attach(rs);

    s.num_viewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ctx->RSGetViewports(&s.num_viewports, s.viewports);
}

void state_restore(ID3D11DeviceContext *ctx, SavedState &s)
{
    ID3D11RenderTargetView *rtvs[8] = {};
    for (int i = 0; i < 8; ++i) rtvs[i] = s.rtvs[i].Get();
    ctx->OMSetRenderTargets(8, rtvs, s.dsv.Get());

    ID3D11ShaderResourceView *srvs[3] = {};
    for (int i = 0; i < 3; ++i) srvs[i] = s.ps_srvs[i].Get();
    ctx->PSSetShaderResources(0, 3, srvs);

    ID3D11SamplerState *samp[1] = { s.ps_samplers[0].Get() };
    ctx->PSSetSamplers(0, 1, samp);

    ctx->VSSetShader(s.vs.Get(), nullptr, 0);
    ctx->PSSetShader(s.ps.Get(), nullptr, 0);

    ctx->IASetInputLayout(s.ia_layout.Get());
    ctx->IASetPrimitiveTopology(s.ia_topology);

    ID3D11Buffer *vb = s.ia_vb.Get();
    ctx->IASetVertexBuffers(0, 1, &vb, &s.ia_vb_stride, &s.ia_vb_offset);
    ctx->IASetIndexBuffer(s.ia_ib.Get(), s.ia_ib_format, s.ia_ib_offset);

    ctx->OMSetBlendState(s.blend.Get(), s.blend_factor, s.blend_sample_mask);
    ctx->OMSetDepthStencilState(s.dss.Get(), s.stencil_ref);
    ctx->RSSetState(s.raster.Get());
    ctx->RSSetViewports(s.num_viewports, s.viewports);
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────────────

bool enabled()                  { return g_enabled.load(std::memory_order_acquire); }
void set_enabled(bool on)       { g_enabled.store(on, std::memory_order_release); }

void on_omset_rt(ID3D11DeviceContext * /*ctx*/,
                 UINT num_views,
                 ID3D11RenderTargetView *const *rtvs)
{
    if (!g_enabled.load(std::memory_order_acquire)) { tl_rt0_is_bb = false; return; }
    if (num_views == 0 || rtvs == nullptr || rtvs[0] == nullptr) { tl_rt0_is_bb = false; return; }

    ID3D11Resource *res = nullptr;
    rtvs[0]->GetResource(&res);
    const bool is_bb = res != nullptr &&
                       res == g_bb_resource_ptr.load(std::memory_order_acquire);
    if (res) res->Release();
    tl_rt0_is_bb = is_bb;
}

void on_draw(ID3D11DeviceContext *ctx)
{
    if (!g_enabled.load(std::memory_order_acquire))               return;
    if (!tl_rt0_is_bb)                                            return;
    if (g_scene_clean_captured.load(std::memory_order_acquire))   return;

    // First BB-targeted draw of the frame. This is almost always the
    // host application's post-FX composite blit (a fullscreen triangle).
    // After it has executed, the BB holds the pristine post-processed
    // scene with no UI on it yet — perfect snapshot point.
    const uint32_t prev = g_bb_draw_count.fetch_add(1, std::memory_order_acq_rel);
    if (prev != 0)
        return;

    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_scene_clean_captured.load(std::memory_order_relaxed)) return;
    if (!g_scene_clean_tex)                                     return;

    ID3D11Resource *bb_res = g_bb_resource_ptr.load(std::memory_order_acquire);
    if (!bb_res) return;
    ctx->CopyResource(g_scene_clean_tex.Get(), bb_res);
    g_scene_clean_captured.store(true, std::memory_order_release);
}

void on_swapchain_present_begin(ID3D11DeviceContext *ctx,
                                IDXGISwapChain *sc,
                                ID3D11Resource *back_buffer_resource)
{
    if (!g_enabled.load(std::memory_order_acquire)) return;
    if (!ctx || !sc || !back_buffer_resource)       return;

    std::lock_guard<std::mutex> lk(g_mtx);

    ComPtr<ID3D11Device> dev;
    ctx->GetDevice(&dev);
    if (!ensure_pipeline_locked(dev.Get()))                      return;
    if (!ensure_per_resolution_locked(dev.Get(), back_buffer_resource)) return;

    g_bb_resource_ptr.store(back_buffer_resource, std::memory_order_release);

    // Snapshot BB-with-UI BEFORE any effect runs. After render_effects has
    // mangled the BB the compositing pass uses this snapshot to restore
    // the original UI pixels.
    ctx->CopyResource(g_bb_with_ui_tex.Get(), back_buffer_resource);

    // Fallback: if no Draw* on the BB happened this frame (e.g. host app
    // drew nothing — extremely unlikely but possible during loading), the
    // scene_clean texture would still hold last frame's data. To avoid that
    // bleeding into the diff, mirror bb_with_ui into scene_clean so the
    // diff evaluates to 0 everywhere and effects apply unmasked.
    if (!g_scene_clean_captured.load(std::memory_order_acquire))
    {
        ctx->CopyResource(g_scene_clean_tex.Get(), back_buffer_resource);
    }
}

void on_after_render_effects(ID3D11DeviceContext *ctx,
                             IDXGISwapChain * /*sc*/,
                             ID3D11Resource *back_buffer_resource)
{
    if (!g_enabled.load(std::memory_order_acquire)) return;
    if (!ctx || !back_buffer_resource)              return;
    if (!g_pipeline_ready.load(std::memory_order_acquire)) return;

    std::lock_guard<std::mutex> lk(g_mtx);
    if (!g_scene_clean_tex || !g_bb_with_ui_tex || !g_effects_result_tex || !g_bb_rtv)
        return;

    // Read back the post-effects BB into effects_result so the PS can sample it.
    ctx->CopyResource(g_effects_result_tex.Get(), back_buffer_resource);

    SavedState s;
    state_save(ctx, s);

    ID3D11RenderTargetView *rtv = g_bb_rtv.Get();
    ctx->OMSetRenderTargets(1, &rtv, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width    = static_cast<FLOAT>(g_bb_width);
    vp.Height   = static_cast<FLOAT>(g_bb_height);
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);

    ctx->IASetInputLayout(nullptr);
    ID3D11Buffer *null_vb = nullptr;
    UINT          zero    = 0;
    ctx->IASetVertexBuffers(0, 1, &null_vb, &zero, &zero);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ctx->VSSetShader(g_vs.Get(), nullptr, 0);
    ctx->PSSetShader(g_ps.Get(), nullptr, 0);

    ID3D11ShaderResourceView *srvs[3] = {
        g_effects_result_srv.Get(),
        g_bb_with_ui_srv.Get(),
        g_scene_clean_srv.Get(),
    };
    ctx->PSSetShaderResources(0, 3, srvs);

    ID3D11SamplerState *samp = g_sampler.Get();
    ctx->PSSetSamplers(0, 1, &samp);

    const FLOAT one[4] = {1, 1, 1, 1};
    ctx->OMSetBlendState(g_blend.Get(), one, 0xffffffffu);
    ctx->OMSetDepthStencilState(g_dss.Get(), 0);
    ctx->RSSetState(g_raster.Get());

    ctx->Draw(3, 0);

    // Unbind our SRVs first to avoid hazard warnings when the next pass
    // tries to render to the BB resource (which we just sampled from
    // via effects_result_srv).
    ID3D11ShaderResourceView *null_srvs[3] = { nullptr, nullptr, nullptr };
    ctx->PSSetShaderResources(0, 3, null_srvs);

    state_restore(ctx, s);
}

void on_present_end()
{
    g_scene_clean_captured.store(false, std::memory_order_release);
    g_bb_draw_count.store(0, std::memory_order_release);
}

void on_swapchain_invalidate()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    release_per_resolution_locked();
    g_bb_resource_ptr.store(nullptr, std::memory_order_release);
}

} // namespace mariusfx::ui_safe_mask
