/*
 * MariusFX preview — stub for `reshade::runtime` and the slice of
 * `reshade::api::effect_runtime` that `ui.cpp` actually uses.
 *
 * In the production DLL build, ui.cpp #includes the real ReShade
 * headers and links into `reshade::runtime`. In the standalone preview
 * `.exe`, we want to compile the SAME ui.cpp source against a tiny
 * stand-in that returns hard-coded mock data, so we can see UI changes
 * without rebooting FiveM.
 *
 * Design:
 *   - This header redefines the same names in the same namespaces.
 *   - It is concrete (no pure virtuals) so ui.cpp's `rt->method()` calls
 *     resolve to direct calls into the mock.
 *   - The shape of the public methods matches ReShade's so ui.cpp
 *     compiles unchanged.
 *
 * Activate by defining `MARIUSFX_PREVIEW` for the preview translation
 * units only.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace reshade {
namespace api {

// ── Format enum (only the values referenced by ui.cpp) ─────────────────────
enum class format : uint32_t {
    unknown = 0,
    r16_float, r32_float,
    r16_sint,  r32_sint,
    r16_uint,  r32_uint,
    r32_typeless,    // ReShade encodes booleans here.
};

// ── Opaque handles ─────────────────────────────────────────────────────────
struct effect_technique         { uint64_t handle; };
struct effect_uniform_variable  { uint64_t handle; };

// ── Forward decls ──────────────────────────────────────────────────────────
class effect_runtime;
typedef void (*effect_technique_callback)(effect_runtime *, effect_technique, void *);
typedef void (*effect_uniform_callback) (effect_runtime *, effect_uniform_variable, void *);

// ── Stub effect_runtime ────────────────────────────────────────────────────
// All per-handle data accessors that ui.cpp's lambdas call via `r->...`.
class effect_runtime {
public:
    virtual ~effect_runtime() = default;

    // Technique queries.
    void get_technique_name        (effect_technique t, char *out, size_t *cap);
    void get_technique_effect_name (effect_technique t, char *out, size_t *cap);
    bool get_technique_state       (effect_technique t);
    bool get_annotation_bool_from_technique  (effect_technique t, const char *name, bool *out,    size_t count);
    bool get_annotation_string_from_technique(effect_technique t, const char *name, char *out,    size_t *cap);

    // Uniform queries.
    void get_uniform_variable_name(effect_uniform_variable u, char *out, size_t *cap);
    void get_uniform_variable_type(effect_uniform_variable u, format *base, uint32_t *rows, uint32_t *cols, uint32_t *arr);
    bool get_annotation_string_from_uniform_variable(effect_uniform_variable u, const char *name, char *out,   size_t *cap);
    bool get_annotation_float_from_uniform_variable (effect_uniform_variable u, const char *name, float *out,  size_t count);
    bool get_annotation_int_from_uniform_variable   (effect_uniform_variable u, const char *name, int32_t *out,size_t count);

    void get_uniform_value_bool (effect_uniform_variable u, bool     *out, size_t count, size_t array_index);
    void get_uniform_value_float(effect_uniform_variable u, float    *out, size_t count, size_t array_index);
    void get_uniform_value_int  (effect_uniform_variable u, int32_t  *out, size_t count, size_t array_index);
    void get_uniform_value_uint (effect_uniform_variable u, uint32_t *out, size_t count, size_t array_index);

    void set_uniform_value_bool (effect_uniform_variable u, const bool     *in, size_t count, size_t array_index);
    void set_uniform_value_float(effect_uniform_variable u, const float    *in, size_t count, size_t array_index);
    void set_uniform_value_int  (effect_uniform_variable u, const int32_t  *in, size_t count, size_t array_index);
    void set_uniform_value_uint (effect_uniform_variable u, const uint32_t *in, size_t count, size_t array_index);
};

} // namespace api

// ── Stub runtime ───────────────────────────────────────────────────────────
// Higher-level methods that ui.cpp calls via `rt->...` outside lambdas.
class runtime : public api::effect_runtime {
public:
    runtime();

    // Technique / uniform iteration.
    void enumerate_techniques       (const char *effect_name, api::effect_technique_callback cb, void *user_data);
    void enumerate_uniform_variables(const char *effect_name, api::effect_uniform_callback   cb, void *user_data);

    void set_technique_state(api::effect_technique t, bool enabled);
    void reset_uniform_value(api::effect_uniform_variable u);

    // Preset.
    void get_current_preset_path(char *out, size_t *cap);
    void set_current_preset_path(const char *path);
    void save_current_preset();
    void export_current_preset(const char *path) const;

    // Global effect toggle.
    bool get_effects_state();
    void set_effects_state(bool enabled);

    // MariusFX additions.
    bool mariusfx_get_performance_mode() const;
    void mariusfx_set_performance_mode(bool v);
    void mariusfx_reload_all();
    void mariusfx_get_technique_timing(api::effect_technique t, uint64_t *cpu_ns, uint64_t *gpu_ns) const;

    // Embedded legacy panels — drawn as a placeholder card in the preview.
    void draw_gui_settings();
    void draw_gui_statistics();
    void draw_gui_log();
    void draw_gui_addons();
};

} // namespace reshade
