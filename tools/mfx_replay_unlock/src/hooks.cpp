// hooks.cpp ─────────────────────────────────────────────────────────
//
// Implementation of the Replay Unlock Winsock interceptor. See
// hooks.hpp for the rationale and the safety contract.
//
// We hook three functions in ws2_32.dll:
//
//     connect       (IPv4 / sockaddr_in)
//     WSAConnect    (Unicode async / WSAOVERLAPPED variant)
//     getaddrinfo   (hostname → addresses)
//
// FiveM's HttpClient is built on libcurl which is statically linked into
// CitizenGame.dll, so we cannot intercept at the libcurl level (no
// exports). Hooking ws2_32 catches every TCP client regardless of the
// HTTP library above it.
//
// The blocklist (IP/hostnames extracted from the user's 200+ clips by
// scripts/extract_all_endpoints.ps1) lives in `endpoints.inl`, which is
// a generated file under source control. Regenerate after recording on
// a new RP server.
//
// ──────────────────────────────────────────────────────────────────────────

// Order matters here: <winsock2.h> must precede <windows.h>, otherwise
// the legacy <winsock.h> gets pulled in and `connect` becomes a macro that
// expands to the wrong symbol.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "hooks.hpp"
#include "MinHook.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace mfx_unlock
{
    // ── Hardcoded blocklist (extracted from the user's 203 clips) ─────────
    //
    // We embed it instead of reading endpoints.json at runtime so the
    // module has zero file dependencies and survives a missing/renamed
    // endpoints.json. Add new entries when you record on a new RP server.

    static constexpr const char *kBlockedHostnames[] = {
        "cache.bay.life",
        "cachetest.bay.life",
        "join.impulse99.com",
    };

    static constexpr const char *kBlockedIPv4[] = {
        "188.165.51.249",
        "188.165.52.9",
        "26.136.185.62",
        "51.210.203.141",
        "51.210.203.154",
        "51.75.255.198",
    };

    // ── Module state ──────────────────────────────────────────────────────

    static std::atomic<bool>   g_enabled       {false};
    static std::atomic<bool>   g_armed         {false};
    static std::atomic<bool>   g_diagnostic    {false};
    static std::atomic<size_t> g_obs_connects  {0};
    static std::atomic<size_t> g_blk_connects  {0};
    static std::atomic<size_t> g_obs_resolves  {0};
    static std::atomic<size_t> g_blk_resolves  {0};

    // Diagnostic per-destination tally. Populated only while
    // g_diagnostic == true; drained by the hotkey thread every few
    // seconds. Mutex contention is acceptable because the hot path is
    // already serialized by the OS through ws2_32 itself.
    static std::mutex                                    g_obs_mtx;
    static std::unordered_map<std::string, size_t>       g_obs_counts;

    static void record_observation(const char *prefix, const char *value)
    {
        if (!g_diagnostic.load(std::memory_order_acquire) || !value || !*value)
            return;
        std::string key;
        key.reserve(strlen(prefix) + strlen(value));
        key.append(prefix).append(value);
        std::lock_guard<std::mutex> lock(g_obs_mtx);
        ++g_obs_counts[key];
    }

    // Last IPv4 that *successfully* hit the OS-level connect(). Used by
    // the exclusion logic so the user can stay connected to a server
    // whose IP happens to be in the static blocklist.
    static std::mutex          g_last_ip_mtx;
    static std::string         g_last_ip;

    // Lookup table built from kBlockedIPv4 at init().
    // We keep it as 32-bit network-order integers for O(1) check inside
    // the connect() hook.
    static std::unordered_set<uint32_t> g_block_ipv4;

    // Same idea for hostnames, lowercased for case-insensitive match.
    static std::unordered_set<std::string> g_block_hosts;

    // ── Trampolines to the real ws2_32 functions ──────────────────────────

    using connect_fn      = int  (WSAAPI *)(SOCKET, const sockaddr*, int);
    using wsaconnect_fn   = int  (WSAAPI *)(SOCKET, const sockaddr*, int,
                                            LPWSABUF, LPWSABUF, LPQOS, LPQOS);
    using getaddrinfo_fn  = int  (WSAAPI *)(PCSTR, PCSTR,
                                            const ADDRINFOA*, PADDRINFOA*);

    static connect_fn      orig_connect      = nullptr;
    static wsaconnect_fn   orig_wsaconnect   = nullptr;
    static getaddrinfo_fn  orig_getaddrinfo  = nullptr;

    // ── Helpers ───────────────────────────────────────────────────────────

    static std::string lc(std::string s)
    {
        for (auto &c : s) c = char(::tolower((unsigned char)c));
        return s;
    }

    static bool addr_to_string(const sockaddr *sa, char out[64])
    {
        out[0] = '\0';
        if (!sa) return false;

        if (sa->sa_family == AF_INET)
        {
            const auto *in4 = reinterpret_cast<const sockaddr_in*>(sa);
            return inet_ntop(AF_INET,
                             const_cast<IN_ADDR*>(&in4->sin_addr),
                             out, 64) != nullptr;
        }
        if (sa->sa_family == AF_INET6)
        {
            const auto *in6 = reinterpret_cast<const sockaddr_in6*>(sa);
            return inet_ntop(AF_INET6,
                             const_cast<IN6_ADDR*>(&in6->sin6_addr),
                             out, 64) != nullptr;
        }
        return false;
    }

    static bool ipv4_in_blocklist(const sockaddr *sa)
    {
        if (!sa || sa->sa_family != AF_INET) return false;
        const auto *in4 = reinterpret_cast<const sockaddr_in*>(sa);
        return g_block_ipv4.count(in4->sin_addr.S_un.S_addr) != 0;
    }

    static bool is_excluded(const sockaddr *sa)
    {
        char ip[64];
        if (!addr_to_string(sa, ip)) return false;

        std::lock_guard<std::mutex> lock(g_last_ip_mtx);
        return !g_last_ip.empty() && g_last_ip == ip;
    }

    static void remember_successful(const sockaddr *sa)
    {
        char ip[64];
        if (!addr_to_string(sa, ip)) return;
        std::lock_guard<std::mutex> lock(g_last_ip_mtx);
        g_last_ip.assign(ip);
    }

    // ── Hooks ─────────────────────────────────────────────────────────────

    static int WSAAPI hooked_connect(SOCKET s, const sockaddr *name, int len)
    {
        g_obs_connects.fetch_add(1, std::memory_order_relaxed);

        // Diagnostic: record the target IP regardless of block decision.
        char ip[64];
        if (addr_to_string(name, ip))
            record_observation("ip:", ip);

        if (g_enabled.load(std::memory_order_acquire)
            && ipv4_in_blocklist(name)
            && !is_excluded(name))
        {
            g_blk_connects.fetch_add(1, std::memory_order_relaxed);
            WSASetLastError(WSAECONNREFUSED);
            return SOCKET_ERROR;
        }

        const int rc = orig_connect(s, name, len);
        if (rc == 0) remember_successful(name);
        return rc;
    }

    static int WSAAPI hooked_wsaconnect(SOCKET s, const sockaddr *name, int len,
                                        LPWSABUF in_buf, LPWSABUF out_buf,
                                        LPQOS qos, LPQOS gqos)
    {
        g_obs_connects.fetch_add(1, std::memory_order_relaxed);

        char ip[64];
        if (addr_to_string(name, ip))
            record_observation("ip:", ip);

        if (g_enabled.load(std::memory_order_acquire)
            && ipv4_in_blocklist(name)
            && !is_excluded(name))
        {
            g_blk_connects.fetch_add(1, std::memory_order_relaxed);
            WSASetLastError(WSAECONNREFUSED);
            return SOCKET_ERROR;
        }

        const int rc = orig_wsaconnect(s, name, len, in_buf, out_buf, qos, gqos);
        if (rc == 0) remember_successful(name);
        return rc;
    }

    static int WSAAPI hooked_getaddrinfo(PCSTR node, PCSTR service,
                                         const ADDRINFOA *hints,
                                         PADDRINFOA *result)
    {
        g_obs_resolves.fetch_add(1, std::memory_order_relaxed);

        if (node)
            record_observation("host:", node);

        if (g_enabled.load(std::memory_order_acquire) && node)
        {
            const std::string h = lc(node);
            if (g_block_hosts.count(h))
            {
                g_blk_resolves.fetch_add(1, std::memory_order_relaxed);
                if (result) *result = nullptr;
                // EAI_NONAME = "Name does not resolve". Most clients
                // treat this as a fatal lookup failure and don't retry.
                WSASetLastError(WSAHOST_NOT_FOUND);
                return EAI_NONAME;
            }
        }

        return orig_getaddrinfo(node, service, hints, result);
    }

    // ── Init / shutdown ───────────────────────────────────────────────────

    static bool install_hook(LPCSTR mod, LPCSTR fn,
                             LPVOID detour, LPVOID *original)
    {
        const HMODULE m = GetModuleHandleA(mod);
        if (!m) return false;

        FARPROC target = GetProcAddress(m, fn);
        if (!target) return false;

        return MH_CreateHook(reinterpret_cast<LPVOID>(target), detour, original) == MH_OK
            && MH_EnableHook(reinterpret_cast<LPVOID>(target)) == MH_OK;
    }

    bool init()
    {
        // Build the IPv4 lookup set.
        for (const char *s : kBlockedIPv4)
        {
            in_addr a{};
            if (inet_pton(AF_INET, s, &a) == 1)
                g_block_ipv4.insert(a.S_un.S_addr);
        }

        for (const char *s : kBlockedHostnames)
            g_block_hosts.insert(lc(s));

        // Make sure ws2_32 is loaded; FiveM always has it but a clean
        // stand-alone test harness might not at the time we attach.
        LoadLibraryA("ws2_32.dll");

        if (MH_Initialize() != MH_OK)
            return false;

        const bool h1 = install_hook("ws2_32.dll", "connect",
                                     reinterpret_cast<LPVOID>(&hooked_connect),
                                     reinterpret_cast<LPVOID*>(&orig_connect));
        const bool h2 = install_hook("ws2_32.dll", "WSAConnect",
                                     reinterpret_cast<LPVOID>(&hooked_wsaconnect),
                                     reinterpret_cast<LPVOID*>(&orig_wsaconnect));
        const bool h3 = install_hook("ws2_32.dll", "getaddrinfo",
                                     reinterpret_cast<LPVOID>(&hooked_getaddrinfo),
                                     reinterpret_cast<LPVOID*>(&orig_getaddrinfo));

        const bool ok = h1 || h2 || h3;
        g_armed.store(ok);
        return ok;
    }

    void shutdown()
    {
        if (!g_armed.load()) return;

        g_enabled.store(false);
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        g_armed.store(false);

        orig_connect     = nullptr;
        orig_wsaconnect  = nullptr;
        orig_getaddrinfo = nullptr;
    }

    // ── Public toggle / readout ───────────────────────────────────────────

    void set_enabled(bool on)
    {
        g_enabled.store(on, std::memory_order_release);
    }

    bool is_enabled() { return g_enabled.load(std::memory_order_acquire); }
    bool is_armed()   { return g_armed.load(std::memory_order_acquire); }

    stats_t snapshot()
    {
        stats_t s{};
        s.connects_observed = g_obs_connects.load();
        s.connects_blocked  = g_blk_connects.load();
        s.resolves_observed = g_obs_resolves.load();
        s.resolves_blocked  = g_blk_resolves.load();

        std::lock_guard<std::mutex> lock(g_last_ip_mtx);
        const size_t n = g_last_ip.size();
        const size_t cp = (n < sizeof(s.last_successful_ip) - 1)
                            ? n : sizeof(s.last_successful_ip) - 1;
        std::memcpy(s.last_successful_ip, g_last_ip.data(), cp);
        s.last_successful_ip[cp] = '\0';
        return s;
    }

    // ── Diagnostic surface ────────────────────────────────────────────────

    void set_diagnostic(bool on)
    {
        g_diagnostic.store(on, std::memory_order_release);
        if (!on)
        {
            // Drop any accumulated state so the next "ON" pass starts
            // from a clean slate.
            std::lock_guard<std::mutex> lock(g_obs_mtx);
            g_obs_counts.clear();
        }
    }

    bool is_diagnostic() { return g_diagnostic.load(std::memory_order_acquire); }

    void drain_observations(std::vector<observation_t>& out)
    {
        out.clear();
        std::unordered_map<std::string, size_t> swapped;
        {
            std::lock_guard<std::mutex> lock(g_obs_mtx);
            swapped.swap(g_obs_counts);
        }
        out.reserve(swapped.size());
        for (auto &kv : swapped)
            out.push_back({ std::move(const_cast<std::string&>(kv.first)), kv.second });
    }
}
