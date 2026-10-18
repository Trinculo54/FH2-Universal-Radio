// FH2 Universal Radio plugin entry point (fh2-mods\FH2UniversalRadio.dll).
//
// Loaded by the FH2 Mod Platform (platform/, d3dcompiler_47.dll next to
// azimuth.exe). DllMain only spawns the bridge thread; everything else
// (waiting for the game image, patching the radio, HTTP, Spotify) happens
// off the loader lock. fh2mod_init registers the radio with the platform,
// which shows a title-screen notice when a new release is out, and from
// platform 2.2 on makes the radio's call-site patches (platform hooks).

#include <windows.h>

#include "fh2mod.h"
#include "fh2/now_playing.hpp"
#include "fh2/platform_link.hpp"

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

namespace {

// ---- the Mods page (Pause > Settings > MODS, platform 2.3.0 / API 4) ----------------------
// The [takeover] switches of fh2.ini, as On/Off rows. The radio rereads fh2.ini while it
// plays, so a change (saved there straight away) applies from the next song the game loads.
struct ModsRow {
    const char* key;        // also the key in fh2.ini [takeover]
    int def;
    const char* label;
    const char* description;
};
const ModsRow kModsRows[] = {
    {"enabled", 1, "Universal Radio",
     "Your music (Spotify, local files, online radio and the rest) plays on the taken-over station. Off leaves every station as the game has it. "
     "Applies from the next song; switch station away and back to hear it now."},
    {"mute_dj", 1, "Mute the DJ", "Silences the DJ and the station jingles on the taken-over station."},
    {"extend_tracks", 1, "Longer radio tracks",
     "The game changes its own track every 16 minutes instead of every few, which keeps tiny hiccups in your music to a minimum."},
    {"show_metadata", 1, "Song names on the radio display", "Shows your song's title and artist where the game shows the radio's."},
};
std::wstring g_ini;

void mods_row_changed(void* user, const char*, int value) {
    const auto* r = static_cast<const ModsRow*>(user);
    wchar_t key[32];
    MultiByteToWideChar(CP_UTF8, 0, r->key, -1, key, 32);
    WritePrivateProfileStringW(L"takeover", key, value ? L"1" : L"0", g_ini.c_str());
}

void add_mods_rows(const FH2ModApi* api, FH2Mod* mod) {
    if (api->api_version < 4) return;   // an older platform: no Mods page
    for (const ModsRow& r : kModsRows) {
        wchar_t key[32];
        MultiByteToWideChar(CP_UTF8, 0, r.key, -1, key, 32);
        FH2Setting s{};
        s.size        = sizeof s;
        s.key         = r.key;
        s.label       = r.label;
        s.description = r.description;
        s.value       = GetPrivateProfileIntW(L"takeover", key, r.def, g_ini.c_str()) != 0;
        s.on_change   = mods_row_changed;
        s.user        = const_cast<ModsRow*>(&r);
        api->add_setting(mod, &s);
    }
}

} // namespace

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
    FH2Mod* mod = api->register_mod(&info);
    if (!mod) return 1;
    fh2::set_platform(api, mod); // the radio hooks go through the platform from 2.2 on
    // Other mods can ask what's playing (include/fh2universalradio.h).
    if (api->api_version >= 2)
        api->publish_interface(mod, FH2_RADIO_NOW_PLAYING, 1, fh2::now_playing_interface());
    g_ini = ini;
    add_mods_rows(api, mod);
    return 0;
}
