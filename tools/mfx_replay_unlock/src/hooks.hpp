// replay_unlock.hpp ─────────────────────────────────────────────────────────
//
// MariusFX Replay Unlock — Layer 2.
//
// Defeats the FiveM Rockstar Editor "Preparing Clip" infinite hang by
// intercepting the per-process Winsock layer. When the user toggles the
// module ON (typically right before opening the editor), every outbound
// `connect()` to one of the IPs / hostnames embedded in the clip's
// resource manifest is short-circuited to `WSAECONNREFUSED`. FiveM's
// HttpClient sees a connection-refused (a definitive answer rather than
// the 403/timeout-loops it gets from the live CDN), gives up on that
// asset, and the editor falls back to whatever is in the local
// `~/AppData/Local/FiveM/FiveM.app/data/server-cache-priv/` cache (~15GB).
//
// The module is OFF by default. While OFF, the hook trampolines exist
// in memory but every entry is a single-instruction-fast pass-through.
// When ON, the hook checks the destination against the blocklist and
// against `g_last_successful_outbound_ip` (the exclusion that lets the
// user keep playing on a recording server even if its IP appears in the
// blocklist).
//
// Properties:
//   - No clip is touched on disk.
//   - No GTA-internal symbol is hooked. Only stable Win32 (ws2_32.dll).
//   - No global hosts/firewall side effect (per-process scope).
//   - Toggling is instantaneous; activation is fully under user control,
//     never auto-fired by an editor-state heuristic.
//   - Detached completely while OFF, so server anti-cheat sees a clean
//     network path during gameplay.
//
// ──────────────────────────────────────────────────────────────────────────

#pragma once

#include <stddef.h>
#include <string>
#include <vector>

namespace mfx_unlock
{
    // Lifecycle. Called once from MariusFXUI's mfxui_init / mfxui_shutdown.
    // init() returns true when the Winsock hooks were successfully armed;
    // a false return means the module is inert (failures are non-fatal,
    // the rest of the UI keeps working).
    bool init();
    void shutdown();

    // Master switch. Inert while false (every connect() passes through).
    // Toggle from the MariusFX UI panel; safe to call from any thread.
    void set_enabled(bool on);
    bool is_enabled();

    // Whether `init()` succeeded and the hooks are in place. Read-only.
    bool is_armed();

    // Stats for the UI panel.
    struct stats_t
    {
        // Total `connect()` calls observed since init(). Includes ones we
        // passed through.
        size_t connects_observed;

        // Total `connect()` calls we redirected to WSAECONNREFUSED.
        size_t connects_blocked;

        // Total `getaddrinfo()` calls observed since init(). Includes ones
        // we passed through. Useful to confirm DNS layer activity even
        // when no hostname matches the blocklist.
        size_t resolves_observed;

        // Total `getaddrinfo()` calls we redirected to EAI_NONAME.
        size_t resolves_blocked;

        // Last IPv4 we successfully forwarded to the OS connect(). The
        // exclusion logic uses this so the user stays connected to the
        // current server even when its IP is in the static blocklist.
        char last_successful_ip[64]; // INET6_ADDRSTRLEN
    };
    stats_t snapshot();

    // Diagnostic mode. While true, every unique (host, ip) seen by the
    // hooks is recorded into a per-destination counter, draining via
    // `drain_observations()`. Disabled by default: the bookkeeping costs
    // a mutex+map lookup per hook call and is wasted unless we're
    // actively investigating *why* an HTTP layer is busy.
    void set_diagnostic(bool on);
    bool is_diagnostic();

    // Pulls every (label, count) pair seen since the previous drain
    // and resets the counters. The label is one of:
    //     "ip:1.2.3.4"            connect()/WSAConnect target
    //     "host:cache.bay.life"   getaddrinfo() argument
    // The drain operation is atomic; the caller is the log writer
    // thread and racing observations append to a fresh empty map.
    struct observation_t { std::string label; size_t count; };
    void drain_observations(std::vector<observation_t>& out);
}
