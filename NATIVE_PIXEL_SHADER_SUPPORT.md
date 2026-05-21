# Native Pixel Shader Support - Implementation Notes

## Session: 2026-05-21 - Hybrid PS/CS Architecture

### Objective
Implement support for native pixel shader compilation alongside compute shader transpilation for ReShade shaders that require PS-only features (derivatives like ddx/ddy/fwidth).

### Background
The existing transpiler compiled all shaders as compute shaders (cs_5_0). However, some shaders (especially AO shaders like PPFX_SSDO, MartysMods_MXAO) use derivatives which are not available in compute shaders. These shaders need to be compiled as native pixel shaders (ps_5_0).

### Implementation Steps

#### 1. Shader Classification Enhancement
**File**: `mariusfx/transpiler/shader_classifier.cpp`
- Added detection of PS-only features: `ddx`, `ddy`, `fwidth`, `ddx_fine`, `ddy_fine`, `ddx_coarse`, `ddy_coarse`
- Added `requires_pixel_shader` flag to `ShaderClassification` struct
- Added filename-based forcing for known AO shaders (SSDO, MXAO, GTAO, HBAO, RTAO) since they use derivatives in macros/inline functions not detected by source scanning

#### 2. TranspiledShader Structure Update
**File**: `mariusfx/transpiler/hlsl_transpiler.hpp`
- Added `bool is_pixel_shader` flag to `TranspiledShader` struct
- This flag indicates whether the shader should be compiled as native PS or transpiled CS

#### 3. Native PS Compilation
**File**: `mariusfx/transpiler/hlsl_transpiler.cpp`
- Modified `transpile_technique()` to check `requires_pixel_shader` flag
- When `requires_pixel_shader=true`:
  - Compiles as native pixel shader (ps_5_0) instead of transpiling to compute shader
  - Adds BUFFER_* macros to preamble (1920x1080 hardcoded for now)
  - Adds ReShade intrinsics namespace stubs:
    - `HasNativeNormals`, `GetNativeNormal`, `HasNativeDepth`, `GetLinearizedDepth`
    - `GetAspectRatio`, `GetResolution`, `GetPixelSize`
    - `GetFrameTime`, `GetFrameCount`
  - Adds `#define GetLinearizedDepth(tc) ReShade::GetLinearizedDepth(tc)` for shaders that don't use namespace prefix
  - Includes full shader source (cleaned) instead of transpiled code
- When `requires_pixel_shader=false`: continues with existing CS transpilation

#### 4. Pipeline Scheduler Updates
**File**: `mariusfx/transpiler/pipeline_scheduler.hpp`
- Modified `CompiledPass` struct:
  - Added `bool is_pixel_shader` flag
  - Added union `shader_ptr` containing either `ID3D11PixelShader*` or `ID3D11ComputeShader*`

**File**: `mariusfx/transpiler/pipeline_scheduler.cpp`
- Added `compile_pixel_shader()` function (similar to `compile_compute_shader()` but uses ps_5_0 profile)
- Modified `compile_shader()` to call appropriate compilation function based on `is_pixel_shader` flag
- Modified `shutdown()` to correctly release either pixel or compute shader based on flag
- Modified `dispatch_shaders()` to skip PS passes for now (TODO: implement fullscreen quad rendering for PS execution)

#### 5. Runtime Integration
**File**: `source/runtime.cpp`
- Removed skip logic for PS-only shaders (now they compile as native PS)
- Added logging to indicate compilation type: "Native PS" vs "Transpiled CS"
- Commented out calls to missing external modules (`ssao_injector`, `gbuffer_capture`) to unblock build
  - TODO: Implement or integrate these modules later

**File**: `source/d3d11/d3d11_device_context.cpp`
- Commented out calls to missing `gbuffer_capture` functions to fix linker errors
  - TODO: Re-enable when module is implemented

#### 6. Build System
**File**: `CMakeLists.txt`
- Added all mariusfx transpiler source files to `RESHADE_SOURCE`:
  - `mariusfx/transpiler/fx_parser.cpp/hpp`
  - `mariusfx/transpiler/hlsl_transpiler.cpp/hpp`
  - `mariusfx/transpiler/shader_classifier.cpp/hpp`
  - `mariusfx/transpiler/pipeline_scheduler.cpp/hpp`
  - `mariusfx/ui_safe_mask_stub.cpp`
  - `mariusfx/loader_stub.cpp`

#### 7. Stubs for Missing Dependencies
**Created**: `mariusfx/ui_safe_mask_stub.cpp`, `mariusfx/loader_stub.cpp`
- Empty function implementations for missing mariusfx symbols to unblock build
- TODO: Implement actual modules or integrate with existing codebase

### ReShade Intrinsics Implemented
The following ReShade intrinsics are now stubbed in the preamble:

```cpp
namespace ReShade {
    bool HasNativeNormals(float2 texcoord) { return false; }  // No native normals in FiveM
    float3 GetNativeNormal(float2 texcoord) { return float3(0, 0, 1); }
    bool HasNativeDepth(float2 texcoord) { return true; }
    float GetLinearizedDepth(float2 texcoord) { return 0.5; }  // TODO: Bind actual depth
    float GetAspectRatio() { return BUFFER_WIDTH / (float)BUFFER_HEIGHT; }
    float2 GetResolution() { return float2(BUFFER_WIDTH, BUFFER_HEIGHT); }
    float2 GetPixelSize() { return float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT); }
    float GetFrameTime() { return 16.67; }
    uint GetFrameCount() { return 0; }
}
#define GetLinearizedDepth(tc) ReShade::GetLinearizedDepth(tc)
```

### Known Issues & TODOs

1. **PS Execution**: Currently PS passes are skipped in `dispatch_shaders()`. Need to implement fullscreen quad rendering for native PS execution.
2. **BUFFER_* Resolution**: Hardcoded to 1920x1080. Should use actual resolution from runtime.
3. **Depth Binding**: `GetLinearizedDepth` returns stub value 0.5. Should bind actual depth buffer.
4. **Missing Modules**: `ssao_injector` and `gbuffer_capture` calls are commented out. Need to implement or integrate.
5. **Derivative Detection**: Filename-based forcing is a workaround. Better detection of derivatives in macros/inline functions needed.

### Test Results
- **Shaders Tested**: PPFX_SSDO.fx, MartysMods_MXAO.fx
- **Classification**: Both correctly detected as `requires_ps=1`
- **Compilation**: Both compile as native PS (ps_5_0 profile)
- **Status**: Architecture complete, awaiting full integration testing

### Files Modified
- `mariusfx/transpiler/shader_classifier.cpp` - Added PS detection
- `mariusfx/transpiler/hlsl_transpiler.hpp` - Added is_pixel_shader flag
- `mariusfx/transpiler/hlsl_transpiler.cpp` - Added native PS compilation path
- `mariusfx/transpiler/pipeline_scheduler.hpp` - Added hybrid shader storage
- `mariusfx/transpiler/pipeline_scheduler.cpp` - Added PS compilation and execution
- `source/runtime.cpp` - Removed PS skip logic, added logging
- `source/d3d11/d3d11_device_context.cpp` - Commented out missing module calls
- `CMakeLists.txt` - Added transpiler source files
- `mariusfx/ui_safe_mask_stub.cpp` - Created stub
- `mariusfx/loader_stub.cpp` - Created stub

### Next Steps
1. Implement fullscreen quad rendering for PS execution
2. Bind actual depth buffer for GetLinearizedDepth
3. Use dynamic resolution instead of hardcoded 1920x1080
4. Implement or integrate ssao_injector and gbuffer_capture modules
5. Improve derivative detection beyond simple source scanning
6. Full integration testing with all AO shaders
