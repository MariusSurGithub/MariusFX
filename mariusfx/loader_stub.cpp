// Stub implementation for loader (not used in transpiler-only mode)

struct ImVec2 { float x, y; };

namespace reshade::api { struct effect_runtime; }

namespace mariusfx::loader {

int configure_next_window(ImVec2, ImVec2) { return 0; }
void tick() {}
void render(reshade::api::effect_runtime*) {}

} // namespace mariusfx::loader
