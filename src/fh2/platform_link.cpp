#include "fh2/platform_link.hpp"

#include <windows.h>

#include <atomic>

namespace fh2 {

namespace {
std::atomic<const FH2ModApi*> g_api{nullptr};
std::atomic<FH2Mod*> g_mod{nullptr};
std::atomic<bool> g_gave_up{false};
HANDLE g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
} // namespace

void set_platform(const FH2ModApi* api, FH2Mod* mod) noexcept {
    g_mod = mod;
    g_api = api;
    if (g_ready) SetEvent(g_ready);
}

const FH2ModApi* wait_for_platform(uint32_t ms) noexcept {
    if (!g_api && !g_gave_up && g_ready && WaitForSingleObject(g_ready, ms) != WAIT_OBJECT_0) g_gave_up = true;
    return g_api;
}

const FH2ModApi* platform_v3() noexcept {
    const FH2ModApi* api = g_api;
    return api && api->api_version >= 3 && g_mod ? api : nullptr;
}

FH2Mod* platform_mod() noexcept { return g_mod; }

} // namespace fh2
