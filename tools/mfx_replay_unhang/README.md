# MariusFX Replay Unhang -- L1

OS-level network sinkhole that fixes the FiveM Rockstar Editor "Preparing
Clip" infinite hang **without modifying any clip on disk** and **without
hooking the GTA process**.

## How it works

When the Rockstar Editor opens a clip recorded under FiveM, FiveM walks
the clip's embedded resource manifest (typically 200+ resources, ~100k
URLs) and tries to refetch every asset from the recording server's CDN
and game server endpoints. If those endpoints are unreachable / return
403 / require an auth that the offline editor cannot produce, FiveM's
HttpClient retries forever -> **infinite hang on "Preparing Clip"**.

This toolkit:

1. Statically scans every `.clip` in your Rockstar Editor folder and
   produces `endpoints.json` listing every CDN hostname and raw IP they
   reference.
2. Adds a `127.0.0.1` redirect in `hosts` for every hostname.
3. Adds a Windows Firewall outbound block rule for every raw IP.

When FiveM tries to reach those endpoints during clip load, it gets
`ECONNREFUSED` (hosts -> localhost) or `WSA*BLOCKED*` (firewall) and --
unlike the 403-loops -- a connection-refused is a fatal answer that
FiveM gives up on, falling back to whatever resources are already in the
local 15GB FiveM compcache.

## Properties

| | |
|---|---|
| Modifies clips on disk           | NO  |
| Modifies GTA process / hooks DLL | NO  |
| Network access required          | NO (works fully offline) |
| Retroactive on existing clips    | YES (works on every clip on disk) |
| Ban risk                         | None: nothing FiveM can detect |
| Reversible                       | One-shot remove_unhang.ps1 |

## Files

- `endpoints.json` -- generated manifest, one entry per unique host / IP
  observed across your clips. Regenerate with
  `scripts/extract_all_endpoints.ps1` whenever you record clips on a new
  RP server.
- `apply_unhang.ps1` -- adds hosts entries + firewall rules.
  *(requires admin / elevation)*
- `remove_unhang.ps1` -- complete rollback. *(requires admin)*
- `hosts_backups/` -- automatic backups of `hosts` on every apply/remove.

## Usage

```powershell
# 1. Refresh the manifest (only needed when you add clips from a new server)
powershell -ExecutionPolicy Bypass -File scripts\extract_all_endpoints.ps1

# 2. Deploy. Right-click PowerShell -> Run as administrator.
powershell -ExecutionPolicy Bypass -File apply_unhang.ps1

# 3. Open FiveM, open Rockstar Editor, open any of your clips.
#    They should now load instead of hanging.

# 4. To roll back when you no longer need it (also as admin):
powershell -ExecutionPolicy Bypass -File remove_unhang.ps1
```

## Caveats

- Once `apply_unhang.ps1` is active, you cannot reach those endpoints
  from any program on this machine -- including connecting to the RP
  server itself. Roll back before playing on the RP server again.
  *(Future work: scope the firewall rules to the GTAProcess /
  CitizenGame binary only.)*
- If the Rockstar Editor still hangs after this, the resource fetcher's
  retry policy is more stubborn than expected and we need an actual
  HTTPS sinkhole returning fast 404s -- see L2 in the roadmap.

## Roadmap

- L2: local HTTPS sinkhole on `127.0.0.1:443` returning HTTP 404 for any
  path, with a per-machine MariusFX CA cert, so FiveM gets a definitive
  "missing" answer rather than a connection error.
- Scope firewall rules to `GTAProcess.exe` / `CitizenGame.exe` so the
  block doesn't leak to the rest of the system.
- Layer 3: panel inside the MariusFX overlay that toggles the unhang
  state, lists endpoints, and shows per-clip preflight status.
