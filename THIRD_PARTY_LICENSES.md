# Third-party components

MariusFX bundles or links to the following open-source components.
Each one ships under its respective license; see `LICENSE.md` and
the individual files inside `deps/` and `source/` for the verbatim
copyright notices.

| Component                  | License        | Use |
|----------------------------|----------------|-----|
| Post-processing engine     | BSD 3-Clause   | Effect compiler, runtime, hook manager, INI parsing |
| Dear ImGui                 | MIT            | Overlay UI |
| MinHook                    | BSD 2-Clause   | x86_64 function hooking |
| stb / stb_image            | Public domain  | Image loading utilities |
| glad                       | MIT            | OpenGL function loader (legacy) |
| utfcpp                     | Boost 1.0      | UTF-8 string handling |
| SPIRV-Headers              | Apache 2.0     | Vulkan / SPIR-V codegen support |
| VulkanMemoryAllocator      | MIT            | Vulkan memory pool |
| fpng                       | MIT            | Fast PNG screenshot encoder |
| DirectX-Headers            | MIT            | D3D12 headers |
| OpenXR-SDK                 | Apache 2.0     | VR headset support |
| jxl_simple_lossless        | Apache 2.0     | JXL screenshot encoder |
| DM Sans / DM Mono fonts    | OFL 1.1        | Overlay UI typography |
| Lucide icons               | ISC            | Overlay UI iconography |

The complete license texts are preserved verbatim in `LICENSE.md`
(BSD 3-Clause, the primary license) and inside each dependency
folder under `deps/`.

If you redistribute MariusFX in any form, please keep this file
and `LICENSE.md` together with the binary.
