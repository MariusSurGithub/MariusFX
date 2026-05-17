# mfx_replay_unlock.asi

**Standalone** FiveM plugin that bypasses the Rockstar Editor "Preparing
Clip" infinite hang. Not part of MariusFX — they live in the same repo
for convenience but share zero code.

## Why

FiveM clips embed a `manifest.json` listing every CDN endpoint (cars,
peds, props) the clip needs. When you open the clip, FiveM's
`HttpClient` re-requests every asset from the recording server's CDN.
If the asset is gone (server wiped, asset rotated, server offline) the
HTTP layer enters an infinite retry loop on 403/timeout and the editor
never finishes loading.

`mfx_replay_unlock` intercepts the per-process Winsock layer
(`connect`, `WSAConnect`, `getaddrinfo`) and short-circuits each of
those calls to `WSAECONNREFUSED` / `EAI_NONAME` so the HTTP layer
gives up immediately and falls back to the **local clip cache** in
`%LOCALAPPDATA%\FiveM\FiveM.app\data\server-cache-priv\` (~15 GiB on
a typical RP profile, enough to render most clips standalone).

## How it loads

FiveM's official ASI loader (`code/components/asi-five` in the
citizenfx/fivem source tree) scans `plugins\*.asi` at game start and
loads each one whose embedded `FX_ASI_BUILD` Windows resource matches
the current game build. No DLL hijack, no injector, no anti-cheat
red-flag — this is the supported plugin path.

## Toggle

- **Default**: OFF. Hooks are installed but every `connect` passes
  through unchanged. Anti-cheat sees a clean network path.
- **F9**: toggle ON ↔ OFF. While ON, `connect()` calls to any of the
  embedded blocklist endpoints (`endpoints.hpp`) return
  `WSAECONNREFUSED`. Other connects pass through; in particular the
  IP of the server you're currently connected to is dynamically
  excluded (see `g_last_successful_outbound_ip`).
- **Recommended workflow**: launch FiveM, press F9 **before** opening
  the Rockstar Editor, load your clip, render it, press F9 again to
  re-enable normal network behaviour before rejoining a server.

## Build

    cd tools\mfx_replay_unlock
    .\build.bat

Outputs `bin\mfx_replay_unlock.asi`. Requires MSVC 2019+ with the C++
x64 toolchain on PATH (or vswhere-discoverable). MinHook headers are
read from `..\..\deps\minhook` — that's the only repo-cross-reference
in the build.

## Deploy

    .\deploy.ps1

Copies the .asi into `%LOCALAPPDATA%\FiveM\FiveM.app\plugins\`. Close
FiveM first — ASIs are loaded once at game start and only released on
process exit.

## Logs

    %LOCALAPPDATA%\FiveM\FiveM.app\plugins\mfx_replay_unlock.log

Tail with:

    Get-Content "$env:LOCALAPPDATA\FiveM\FiveM.app\plugins\mfx_replay_unlock.log" -Wait -Tail 50

You'll see lines like:

    [18:32:14.121] mfx_replay_unlock loaded. Hooks armed=yes. Toggle: F9. Default: OFF.
    [18:34:02.847] F9 pressed -> hooks ENABLED
    [18:34:07.848] +5s : 12 connect blocked, 4 DNS blocked (totals 12 / 4)
    [18:34:12.849] +5s : 0 connect blocked, 0 DNS blocked (totals 12 / 4)
    [18:35:01.512] F9 pressed -> hooks disabled

## File layout

    tools/mfx_replay_unlock/
      src/
        dllmain.cpp                 DllMain + F9 thread + log writer
        hooks.cpp                   MinHook arming + connect/WSAConnect/getaddrinfo handlers
        hooks.hpp                   Public surface: init/shutdown/set_enabled/snapshot
        endpoints.hpp               Hardcoded blocklist (IPs + hostnames)
        mfx_replay_unlock.rc        FX_ASI_BUILD resources + VERSIONINFO
      build.bat                     Standalone MSVC build, no dependency on MariusFX
      deploy.ps1                    Build + copy to FiveM plugins\
      README.md                     This file.

## Risk model

- The plugin is **per-process**, only ws2_32 inside the GTAProcess is
  patched. No system-wide effect, no hosts file change, no firewall rule.
- Hooks are **trampoline-based** (MinHook). Disabling them via the F9
  toggle is one atomic store; the trampolines remain in memory but
  every entry path early-outs to the original function.
- We never touch the clip files on disk, never modify a savegame, never
  hook a RAGE-internal symbol. The whole thing is one DLL that talks
  to ws2_32 and nothing else.
- The official FiveM ASI path is **MP-disable-able by the server**. If
  a server says "no plugins", FiveM won't load this .asi at all on
  that server, which is fine — you only need it for editor sessions
  anyway.
