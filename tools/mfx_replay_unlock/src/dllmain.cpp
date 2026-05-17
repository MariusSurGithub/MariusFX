// dllmain.cpp ──────────────────────────────────────────────────────────────
//
// Entry point for mfx_replay_unlock.asi — a standalone FiveM plugin
// (NOT part of MariusFX) that bypasses the Rockstar Editor's "Preparing
// Clip" infinite hang by intercepting per-process Winsock connect() and
// getaddrinfo() calls. See README.md for the design rationale and
// `endpoints.hpp` for the blocklist.
//
// Loading model:
//   FiveM's `asi-five` component (code/components/asi-five) scans
//   %LOCALAPPDATA%\FiveM\FiveM.app\plugins\*.asi at game startup. For
//   each matching file it:
//     1. Calls LoadLibraryEx(... LOAD_LIBRARY_AS_DATAFILE) and looks
//        for a Windows resource `FX_ASI_BUILD <game_build>`. If the
//        resource is missing for the running build, the ASI is rejected
//        with a `script:shv` console error (see `mfx_replay_unlock.rc`
//        for the resource declaration; we ship every build that has
//        been observed to import .asi files since 2189).
//     2. Verifies the file is NOT a CLR assembly (managed code is
//        blocked outright by FiveM).
//     3. Runs a small blacklist (openiv.asi, scripthookvdotnet.asi,
//        fspeedometerv.asi) — we are none of those, so we pass.
//     4. Calls LoadLibrary() on the .asi. Our DllMain runs here.
//
// At that point, ws2_32.dll is already mapped (it's a static import of
// every Win32 process), so we can hook it immediately. The actual
// hook-arming code lives in `hooks.cpp`; this file is just the lifecycle
// glue + the user-facing toggle.
//
// Toggle UX (this file):
//   * Default OFF: hooks installed but disabled (pass-through). Gameplay
//     is 100% normal and anti-cheat sees a clean network path.
//   * F9 hotkey: toggles enabled/disabled. Polled at 50 ms from a
//     dedicated thread (GetAsyncKeyState; the keypress is consumed
//     edge-triggered so holding F9 doesn't spam toggles).
//   * Status is written to:
//       %LOCALAPPDATA%\FiveM\FiveM.app\plugins\mfx_replay_unlock.log
//     on every state change and on every blocked connect/getaddrinfo
//     burst (debounced to one line / 500 ms / category).
//
// Threading:
//   * DllMain spawns ONE worker thread (`hotkey_thread`).
//   * That thread owns: hotkey polling, log file writing, periodic
//     stats summary.
//   * The hook callbacks (hooks.cpp) only touch atomics and a tiny
//     mutex-guarded LRU set, both of which are safe to call from
//     arbitrary RAGE network threads.
//
// ──────────────────────────────────────────────────────────────────────────

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <string>
#include <vector>

#include "hooks.hpp"

namespace
{
    // ── Module / log path ──────────────────────────────────────────────
    //
    // Log path is derived from the .asi's own file location: whatever
    // folder contains us, the log file lives next to it. This always
    // ends up being `<FiveM.app>\plugins\` because that's where FiveM
    // loads us from, but resolving it dynamically means we don't have
    // to hard-code FOLDERID_LocalAppData and we still work if the user
    // moves FiveM.app to another drive.
    HMODULE g_self_module = nullptr;

    std::string g_log_path;

    void compute_log_path()
    {
        char buf[MAX_PATH] = {};
        DWORD n = GetModuleFileNameA(g_self_module, buf, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
        {
            g_log_path = "mfx_replay_unlock.log"; // fallback: CWD
            return;
        }
        std::string p(buf, n);
        const size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos)
            p.resize(slash + 1);
        else
            p.clear();
        p += "mfx_replay_unlock.log";
        g_log_path = std::move(p);
    }

    // ── Logging ─────────────────────────────────────────────────────────
    //
    // Append-only, fsync'd per line, ~1 KiB ceiling per format call.
    // Format: [HH:MM:SS] <line>. We avoid CRT statics that aren't safe
    // to call from DllMain (we only call this from worker thread).
    void log_line(const char *fmt, ...)
    {
        char body[1024];
        va_list ap;
        va_start(ap, fmt);
        const int n = std::vsnprintf(body, sizeof(body), fmt, ap);
        va_end(ap);
        if (n <= 0)
            return;

        SYSTEMTIME st;
        GetLocalTime(&st);

        char line[1100];
        const int m = std::snprintf(line, sizeof(line),
            "[%02d:%02d:%02d.%03d] %s\r\n",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, body);
        if (m <= 0)
            return;

        // Use plain Win32 file APIs so we don't pull in CRT FILE* state.
        // FILE_SHARE_READ + APPEND lets Tail.exe / Get-Content -Wait
        // sit on the log without locking us out.
        HANDLE h = CreateFileA(g_log_path.c_str(),
            FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return;
        DWORD written = 0;
        WriteFile(h, line, (DWORD)m, &written, nullptr);
        CloseHandle(h);

        // Also emit to debugger if attached. Lets the user see real-time
        // state via DbgView without opening the log file.
        OutputDebugStringA(line);
    }

    // ── Hotkey thread ───────────────────────────────────────────────────
    //
    // Polls GetAsyncKeyState every 50 ms. We use the "previous state" bit
    // (0x0001) to detect the press edge so holding F9 only toggles once.
    // 50 ms is below the human keypress duration (~80–100 ms) so we never
    // miss a tap, yet small enough that CPU usage is invisible (~0.01%).
    //
    // The thread also dumps a stats summary every 5 s while hooks are
    // enabled, so the user gets passive feedback that the module is
    // doing something without having to open the log.
    std::atomic<bool> g_thread_should_exit{false};
    HANDLE            g_thread_handle = nullptr;

    constexpr int  HOTKEY_TOGGLE_VK  = VK_F9; // toggle block ON/OFF
    constexpr int  HOTKEY_DUMP_VK    = VK_F8; // force immediate observations dump
    constexpr UINT POLL_INTERVAL_MS  = 50;
    constexpr UINT STATS_INTERVAL_MS = 5000;

    // Pretty-print one observation drain. Sorted by descending count so
    // the most-active destinations float to the top of the log.
    void log_observations(const char *header)
    {
        std::vector<mfx_unlock::observation_t> obs;
        mfx_unlock::drain_observations(obs);
        if (obs.empty())
        {
            log_line("%s : (no ws2_32 activity since last drain)", header);
            return;
        }
        std::sort(obs.begin(), obs.end(),
            [](const mfx_unlock::observation_t &a, const mfx_unlock::observation_t &b) {
                return a.count > b.count;
            });
        log_line("%s : %zu unique destinations:", header, obs.size());
        // Cap to top 30 entries to keep the log readable; if the rest
        // matter we'll see them in subsequent drains.
        const size_t cap = obs.size() < 30 ? obs.size() : 30;
        for (size_t i = 0; i < cap; ++i)
            log_line("    %5zu x %s", obs[i].count, obs[i].label.c_str());
        if (obs.size() > cap)
            log_line("    ... +%zu more not shown", obs.size() - cap);
    }

    DWORD WINAPI hotkey_thread(LPVOID)
    {
        compute_log_path();

        // Diagnostic mode is on by default in this build. Cost is one
        // mutex+map insert per ws2_32 connect/getaddrinfo call, which is
        // negligible compared to the syscall itself. Lets us answer "is
        // anything actually going through the socket layer right now?"
        // without a rebuild.
        mfx_unlock::set_diagnostic(true);

        log_line("mfx_replay_unlock loaded. Hooks armed=%s. Diagnostic=%s.",
                 mfx_unlock::is_armed()      ? "yes" : "NO",
                 mfx_unlock::is_diagnostic() ? "ON"  : "off");
        log_line("Hotkeys: F9 = toggle block (currently OFF), F8 = dump observations now.");

        UINT  ms_since_stats   = 0;
        size_t last_obs_c      = 0;
        size_t last_obs_r      = 0;
        size_t last_blk_c      = 0;
        size_t last_blk_r      = 0;

        while (!g_thread_should_exit.load(std::memory_order_relaxed))
        {
            // ── F9 : toggle block ON/OFF (edge-triggered) ─────────────
            if ((GetAsyncKeyState(HOTKEY_TOGGLE_VK) & 0x0001) != 0)
            {
                const bool now = !mfx_unlock::is_enabled();
                mfx_unlock::set_enabled(now);
                log_line("F9 -> hooks %s", now ? "ENABLED" : "disabled");
            }

            // ── F8 : force-dump observations and reset counters ───────
            if ((GetAsyncKeyState(HOTKEY_DUMP_VK) & 0x0001) != 0)
            {
                log_observations("F8 dump");
                const auto s = mfx_unlock::snapshot();
                log_line("    totals: %zu connect (%zu blocked), %zu DNS (%zu blocked)",
                         s.connects_observed, s.connects_blocked,
                         s.resolves_observed, s.resolves_blocked);
            }

            // ── Periodic stats every 5 s ──────────────────────────────
            ms_since_stats += POLL_INTERVAL_MS;
            if (ms_since_stats >= STATS_INTERVAL_MS)
            {
                ms_since_stats = 0;
                const auto s = mfx_unlock::snapshot();
                const size_t dC = s.connects_observed - last_obs_c;
                const size_t dR = s.resolves_observed - last_obs_r;
                const size_t dBC = s.connects_blocked - last_blk_c;
                const size_t dBR = s.resolves_blocked - last_blk_r;

                if (dC != 0 || dR != 0)
                {
                    log_line("+5s : %zu connect (%zu blocked), %zu DNS (%zu blocked)",
                             dC, dBC, dR, dBR);
                    log_observations("+5s top destinations");
                }

                last_obs_c = s.connects_observed;
                last_obs_r = s.resolves_observed;
                last_blk_c = s.connects_blocked;
                last_blk_r = s.resolves_blocked;
            }

            Sleep(POLL_INTERVAL_MS);
        }

        log_line("mfx_replay_unlock unloading.");
        return 0;
    }
}

// ── Win32 entry point ──────────────────────────────────────────────────────
//
// We deliberately do the absolute minimum inside DllMain itself. Per MSDN
// rules we can't call LoadLibrary, can't take the loader lock, can't do
// CRT I/O — but CreateThread is on the official allow-list. So we just:
//   1. Cache our HMODULE for later GetModuleFileName resolution.
//   2. Arm the hooks (hooks.cpp does its own MinHook init inside; that
//      ONLY touches GetProcAddress on ws2_32.dll which is already mapped,
//      so it's loader-lock-safe).
//   3. Spawn the hotkey/log thread.
//
// DLL_PROCESS_DETACH is best-effort: the thread is asked to exit and the
// hooks are uninstalled, but we don't wait on the thread because the
// loader lock is held and Sleep() inside the thread might block on it.
// MinHook's MH_Uninitialize() restores the original bytes synchronously
// so any in-flight connect() returns to GTA's call site without
// trampolining into our (about-to-disappear) hook function.
extern "C" BOOL WINAPI DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_self_module = hModule;
        DisableThreadLibraryCalls(hModule);
        mfx_unlock::init();
        g_thread_handle = CreateThread(nullptr, 0, &hotkey_thread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        g_thread_should_exit.store(true, std::memory_order_relaxed);
        mfx_unlock::shutdown();
        // Intentionally NOT WaitForSingleObject(g_thread_handle): see
        // header comment above. The thread will exit on its next poll.
        if (g_thread_handle)
            CloseHandle(g_thread_handle);
        g_thread_handle = nullptr;
        break;

    default:
        break;
    }
    return TRUE;
}
