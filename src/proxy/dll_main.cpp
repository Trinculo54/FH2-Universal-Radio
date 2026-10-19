// FH2 Universal Radio, an FH2ML v4 mod (mods\universalradio\FH2UniversalRadio.dll).
//
// FH2ML asks for the descriptor at the bottom, then calls Initialize on its
// worker thread (never under the loader lock) when Azimuth starts, before the
// game is mapped. Initialize brings the player's files over from <Azimuth>\
// fh2-radio once, publishes the now-playing interface for other mods, puts the
// radio's switches on the game's Mods page and starts the bridge thread, which
// does everything else (waiting for the game image, patching the radio through
// FH2ML's hooks, HTTP, Spotify). FH2ML shows a title-screen notice when a new
// release is out on GitHub, unless the player turned that off in
// FH2ML\config\FH2ML.json.

#include <windows.h>

#include "FH2ML_v4.h"
#include "fh2/fh2ml_link.hpp"
#include "fh2/now_playing.hpp"

#include <cwctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace fh6 {
void run_bridge(const std::filesystem::path& azimuth_dir, const std::filesystem::path& mod_dir,
                const std::filesystem::path& data_dir) noexcept;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(instance);
    return TRUE;
}

namespace {

const FH2ML_Services_v4* S;
FH2ML_Mod* M;
// Copies of the folders FH2ML hands us: mods\universalradio (what the mod
// ships: ui\, the worker, config.example.toml) and FH2ML\data\universalradio
// (the player's: fh2.ini, config.toml, logs, Spotify login, helpers).
std::wstring g_azimuth_dir, g_mod_dir, g_data_dir;
std::wstring g_ini; // <data>\fh2.ini

DWORD WINAPI bridge_thread(LPVOID) {
    fh6::run_bridge(g_azimuth_dir, g_mod_dir, g_data_dir);
    return 0;
}

// Once: copies the platform-era files (a file, or a folder's contents) into the data
// folder. Files already there win. A marker file remembers that it was done.
void import_from_platform(const FH2ML_ModContext_v4* c,
                          std::initializer_list<std::pair<const wchar_t*, const wchar_t*>> items) {
    std::error_code e;
    const fs::path data = c->data_directory, marker = data / L"imported-from-fh2-mods.txt";
    if (fs::exists(marker, e)) return;
    for (auto [from, to] : items) {
        const fs::path src = fs::path(c->services->azimuth_directory) / from, dst = data / to;
        if (!fs::exists(src, e)) continue;
        fs::create_directories(fs::is_directory(src, e) ? dst : dst.parent_path(), e);
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::skip_existing, e);
    }
    std::ofstream(marker) << "Copied from the FH2 Mod Platform's folders on the first launch under FH2ML.\n";
}

// The bridge only runs inside the game itself: Azimuth hosts the Durango exe
// inside azimuth.exe; Mezcal runs the game exe directly.
bool host_is_game() {
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    std::wstring name = fs::path{std::wstring{buf, n}}.filename().wstring();
    for (wchar_t& ch : name) ch = static_cast<wchar_t>(std::towlower(ch));
    return name == L"azimuth.exe" || name == L"forza_durango_release_final.exe";
}

// ---- the Mods page (Pause > Settings > MODS) -------------------------------------------------
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

void mods_row_changed(void* user, const char*, int value) {
    const auto* r = static_cast<const ModsRow*>(user);
    wchar_t key[32];
    MultiByteToWideChar(CP_UTF8, 0, r->key, -1, key, 32);
    WritePrivateProfileStringW(L"takeover", key, value ? L"1" : L"0", g_ini.c_str());
}

void add_mods_rows() {
    for (const ModsRow& r : kModsRows) {
        wchar_t key[32];
        MultiByteToWideChar(CP_UTF8, 0, r.key, -1, key, 32);
        FH2ML_Setting s{};
        s.size        = sizeof s;
        s.key         = r.key;
        s.label       = r.label;
        s.description = r.description;
        s.value       = GetPrivateProfileIntW(L"takeover", key, r.def, g_ini.c_str()) != 0;
        s.on_change   = mods_row_changed;
        s.user        = const_cast<ModsRow*>(&r);
        s.kind        = FH2ML_SETTING_SWITCH;
        S->AddSetting(M, &s);
    }
    // A row on the radio's page under MODS > Installed mods that opens the dashboard.
    FH2ML_Setting s{};
    s.size        = sizeof s;
    s.key         = "Dashboard";
    s.label       = "Open the dashboard";
    s.description = "Opens the radio's dashboard in your browser, where you choose what plays: Spotify, your own music, "
                    "online radio, YouTube Music, Jellyfin or another app. The game keeps running; switch to the browser to see it.";
    s.kind        = FH2ML_SETTING_LINK;
    s.url         = "http://localhost:8420";
    S->AddSetting(M, &s);
}

int initialize(const FH2ML_ModContext_v4* c, void*) {
    S = c->services;
    M = c->mod;
    try {
        g_azimuth_dir = S->azimuth_directory;
        g_mod_dir     = c->mod_directory;
        g_data_dir    = c->data_directory;
        g_ini         = g_data_dir + L"\\fh2.ini";
        fh2::set_services(S, M); // the radio hooks go through FH2ML

        // The player's files from <Azimuth>\fh2-radio, before anything reads them. Not its
        // ui\, assets\ or worker: those come with the mod, in mods\universalradio, now.
        import_from_platform(c, {{L"fh2-radio\\fh2.ini", L"fh2.ini"},
                                 {L"fh2-radio\\config.toml", L"config.toml"},
                                 {L"fh2-radio\\local_index.json", L"local_index.json"},
                                 {L"fh2-radio\\spotify_cache", L"spotify_cache"},
                                 {L"fh2-radio\\bin", L"bin"},
                                 {L"fh2-radio\\bridge.log", L"bridge.log"},
                                 {L"fh2-radio\\helpers.log", L"helpers.log"}});

        // Other mods can ask what's playing (include/fh2universalradio.h).
        S->PublishInterface(M, FH2_RADIO_NOW_PLAYING, 1, fh2::now_playing_interface());
        add_mods_rows();

        if (!host_is_game()) {
            S->Log(M, "not running inside the game, so the radio stays off");
            return 1;
        }
        // Pin ourselves: the bridge thread and the patched game code call into this
        // module for the life of the process.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&bridge_thread), &pinned);
        HANDLE t = CreateThread(nullptr, 0, bridge_thread, nullptr, 0, nullptr);
        if (!t) {
            S->Log(M, "couldn't start the radio's thread");
            return 0;
        }
        CloseHandle(t);
        return 1;
    } catch (...) {
        S->Log(M, "Initialize failed (out of memory?)");
        return 0;
    }
}

const FH2ML_ModAPI_v4 descriptor{
    sizeof(FH2ML_ModAPI_v4),
    FH2ML_API_VERSION_V4,
    nullptr,
    "FH2 Universal Radio",
    FH2_RADIO_VERSION,
    "Trinculo54/FH2-Universal-Radio",
    "Plays your own music through the game's radio, on Horizon Pulse by default: Spotify, local files, online radio, "
    "YouTube Music, Jellyfin or any Windows app. Choose what plays from the dashboard at localhost:8420 in your browser.",
    0, // update checks: the player's choice, in FH2ML\config\FH2ML.json
    initialize,
    nullptr, // OnGameReady: the control loop waits for the game with WaitForCode
    nullptr, // OnShutdown: the radio's threads run until the process ends
};

} // namespace

FH2ML_V4_EXPORT const FH2ML_ModAPI_v4* FH2ML_GetModAPI_v4(uint32_t host_version) {
    return host_version == FH2ML_API_VERSION_V4 ? &descriptor : nullptr;
}
