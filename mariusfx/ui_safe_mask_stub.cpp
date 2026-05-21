// Stub implementation for ui_safe_mask (not used in transpiler-only mode)

#include <d3d11.h>

namespace mariusfx::ui_safe_mask {

void on_swapchain_present_begin(ID3D11DeviceContext*, IDXGISwapChain*, ID3D11Resource*) {}
void on_after_render_effects(ID3D11DeviceContext*, IDXGISwapChain*, ID3D11Resource*) {}
void on_present_end() {}
bool enabled() { return false; }
void on_draw(ID3D11DeviceContext*) {}
void on_omset_rt(ID3D11DeviceContext*, unsigned int, ID3D11RenderTargetView* const*) {}

} // namespace mariusfx::ui_safe_mask
