# MariusHybridFX-Reshade

A fork of [crosire/reshade](https://github.com/crosire/reshade) tailored for
**FiveM** with two core differentiators:

1. **UI-safe post-processing** — every effect (bloom, sepia, color grading, …)
   is masked away from HUD/NUI/RageUI pixels via a backbuffer-diff technique
   so post-FX never bleeds onto the phone, the HUD or any in-game menu.

2. **Custom modern UI** — a redesigned ImGui overlay inspired by a clean
   dark-blue dashboard layout (titlebar tabs, sidebar shader list with
   filter pills, detail panel with parameters + live preview + GPU cost,
   bottombar with shortcuts).

Existing **ReShade `.fx` shaders and `.ini` presets work out of the box** —
this fork keeps the entire ReShade FX compiler and runtime.

## Status

- [x] Recon (build verified locally with VS 2022 17.14, Windows SDK 10.0.26100)
- [ ] BB-diff UI masking integrated into `runtime::on_present`
- [ ] Custom UI replacing `runtime_gui.cpp::draw_gui*`
- [ ] FiveM-specific glue (RawInput camera freeze, vtable healing)
- [ ] Distribution as `dxgi.dll` plugin

## Project layout

```
.                   ← upstream ReShade source (BSD-3, unmodified except
                      where strictly needed for our hooks)
├── source/         ← ReShade engine (effect compiler, runtime, …)
│   └── runtime_gui.cpp ← will be largely replaced by mariusfx UI
├── deps/           ← imgui, minhook, stb, glad, vma, etc.
└── mariusfx/       ← OUR additions (BB-diff, custom UI, FiveM glue)
    ├── ui_safe_mask/   – BB-diff capture + apply pass
    ├── ui/             – custom theme, layout, widgets
    └── fivem_glue/     – RawInput hook, vtable heal
```

## Building

Same as upstream ReShade: open `ReShade.sln` in VS 2022, select Release
`64-bit`, build. Outputs `bin/x64/Release/ReShade64.dll` (rename to
`dxgi.dll` and drop into `%LOCALAPPDATA%\FiveM\FiveM.app\plugins\` to test
in FiveM).

## Origin & license

This is a derivative work of ReShade by Patrick Mours (`crosire`),
licensed under [BSD 3-Clause](LICENSE.md). All original ReShade
copyright headers are preserved verbatim. New files added by this fork
live under `mariusfx/` and are also BSD-3 unless stated otherwise.

Upstream repository: <https://github.com/crosire/reshade>

Track upstream changes:
```
git remote -v          # upstream → crosire/reshade, origin → our fork
git fetch upstream
git merge upstream/main
```
