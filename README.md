# MariusFX

A modern post-processing injector for **FiveM** with pixel-perfect
UI masking. HUD, NUI overlays and in-game menus stay completely
untouched while every effect — bloom, color grading, sharpen,
sepia, vignette… — runs full quality on the actual scene.

## Why MariusFX

Existing post-processing solutions for GTA V / FiveM bleed all
over the HUD: the minimap glows from bloom, the phone gets
desaturated by color grading, and the chat box looks washed out.
MariusFX detects UI pixels at the GPU level via a backbuffer-diff
mask and lerps them back verbatim after every effect pass — so
your scene gets the cinematic look while your interface stays
crisp.

## Features

- **UI-safe by construction** — every shader runs through a
  final compositing pass that restores HUD / NUI / RageUI /
  phone pixels at zero artifact cost
- **Wide effect support** — sepia, bloom, FXAA, color matrix,
  film grain, vignette, chromatic aberration, fake HDR,
  tonemapping, lens flare and more
- **Live editing** — edit a shader on disk, see it update
  in-game on the next frame
- **Per-effect parameter tuning** — sliders for every uniform,
  with min/max from the shader source
- **Preset system** — save and load complete configurations
  per scene / time of day / server
- **GPU profiling overlay** — see exactly how many ms each
  effect costs
- **In-game overlay** — full configuration UI accessible by
  pressing `Home`, with the GTA camera frozen so you can edit
  in peace

## Installing

1. Download the latest `MariusFX.zip` from the [releases page](https://github.com/MariusSurGithub/MariusFX/releases)
2. Extract `dxgi.dll` into `%LOCALAPPDATA%\FiveM\FiveM.app\plugins\`
3. Drop the `MariusFX-shaders/` folder next to the DLL
4. Launch FiveM, press `Home` in-game

That's it. Your existing `.fx` shaders and `.ini` presets work
unchanged — drop them in `MariusFX-shaders/` and they appear in
the overlay.

## Building from source

Requirements: Visual Studio 2022, Windows SDK 10.0.20348+,
Python 3.10+ on the PATH (used by one of the build dependencies).

```
git clone --recurse-submodules https://github.com/MariusSurGithub/MariusFX
cd MariusFX
msbuild MariusFX.sln /p:Configuration=Release /p:Platform=64-bit /m
```

Output: `bin/x64/Release/ReShade64.dll` — rename to `dxgi.dll` for
deployment. *(The internal binary name is kept for compatibility
with effect signing tools; the runtime branding is fully MariusFX.)*

## Hotkeys

| Key | Action |
|---|---|
| `Home` | Toggle the configuration overlay |
| `Del`  | Toggle all effects on/off |
| `End`  | Cycle through presets |

## Roadmap

- [x] Core engine + shader pipeline
- [x] BB-diff UI masking integrated into the post-process chain
- [ ] Custom in-game UI matching the design mockup
- [ ] Live preview thumbnails per effect
- [ ] Drag-and-drop effect ordering
- [ ] Public release

## License

See [LICENSE.md](LICENSE.md) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)
for the licenses governing the source code and embedded
open-source components used by this project.
