// FH2 Universal Radio plugin entry point (fh2-mods\FH2UniversalRadio.dll).
//
// Loaded by the FH2 Mod Platform (platform/, d3dcompiler_47.dll next to
// azimuth.exe). DllMain only spawns the bridge thread; everything else
// (waiting for the game image, patching the radio, HTTP, Spotify) happens
// off the loader lock. fh2mod_init registers the radio with the platform,
// which shows a title-screen notice when a new release is out.

#include <windows.h>

#include "fh2mod.h"

#include <string>

namespace fh6 {
void run_bridge(HMODULE self) noexcept;
}

namespace {
DWORD WINAPI bridge_thread(LPVOID param) {
    fh6::run_bridge(static_cast<HMODULE>(param));
    return 0;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        // Pin ourselves: the bridge thread and the patched game code call
        // into this module for the life of the process.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&bridge_thread), &pinned);
        if (HANDLE t = CreateThread(nullptr, 0, bridge_thread, instance, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}

FH2MOD_EXPORT int fh2mod_init(const FH2ModApi* api) {
    if (!api || api->api_version < 1) return 1;
    const std::wstring ini = std::wstring{api->azimuth_dir} + L"\\fh2-radio\\fh2.ini";
    FH2ModInfo info{};
    info.size             = sizeof info;
    info.name             = "FH2 Universal Radio";
    info.version          = FH2_RADIO_VERSION;
    info.github_repo      = "Trinculo54/FH2-Universal-Radio";
    info.platform_version = FH2MOD_PLATFORM_VERSION;
    info.check_updates    = GetPrivateProfileIntW(L"updates", L"check", 1, ini.c_str()) != 0;
    return api->register_mod(&info) ? 0 : 1;
}
