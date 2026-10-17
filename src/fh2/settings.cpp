#include "fh2/settings.hpp"
#include "fh6/log.hpp"

#include <windows.h>
#include <fstream>
#include <sstream>

namespace fh2 {

namespace {

constexpr const char* kDefaultIni =
    "; FH2 Universal Radio -- Forza Horizon 2 settings\r\n"
    "; Changes are picked up live (on the next track the game loads; flip the\r\n"
    "; in-game station away and back to apply immediately).\r\n"
    "\r\n"
    "[takeover]\r\n"
    "; 1 = turn the mod on, 0 = leave every station vanilla.\r\n"
    "enabled=1\r\n"
    "\r\n"
    "; Which in-game station plays the mod's audio (Spotify, local files, ...).\r\n"
    ";   1 Horizon Pulse      2 Bass Arena        3 Horizon XS\r\n"
    ";   4 Radio Levante      5 Hospital Records  6 Ninja Tune\r\n"
    ";   7 Innovative Leisure\r\n"
    "; 0 = every station plays the mod's audio.\r\n"
    "station=1\r\n"
    "\r\n"
    "; Silence the DJ and station jingles on the taken-over station.\r\n"
    "mute_dj=1\r\n"
    "\r\n"
    "; The game ends each \"track\" after the original song's length (3-6 min).\r\n"
    "; 1 = stretch tracks on the taken-over station so your music is never cut.\r\n"
    "extend_tracks=1\r\n"
    "\r\n"
    "; Show your song title / artist in the game's radio HUD.\r\n"
    "show_metadata=1\r\n";

int read_int(const std::wstring& ini, const wchar_t* key, int def) {
    return static_cast<int>(GetPrivateProfileIntW(L"takeover", key, def, ini.c_str()));
}

std::string attr(const std::string& line, const char* key) {
    const std::string needle = std::string{" "} + key + "=\"";
    auto p                   = line.find(needle);
    if (p == std::string::npos) return {};
    p += needle.size();
    auto e = line.find('"', p);
    if (e == std::string::npos) return {};
    return line.substr(p, e - p);
}

} // namespace

SettingsFile::SettingsFile(std::filesystem::path path) : path_{std::move(path)} {}

void SettingsFile::ensure_exists() const {
    std::error_code ec;
    if (std::filesystem::exists(path_, ec)) return;
    std::ofstream out{path_, std::ios::binary};
    out << kDefaultIni;
}

bool SettingsFile::refresh() {
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path_, ec);
    if (ec || stamp == stamp_) return false;
    stamp_ = stamp;

    const std::wstring ini = path_.wstring();
    enabled_.store(read_int(ini, L"enabled", 1) != 0);
    int st = read_int(ini, L"station", 1);
    if (st < 0 || st > 9) st = 1;
    station_.store(st);
    mute_dj_.store(read_int(ini, L"mute_dj", 1) != 0);
    extend_.store(read_int(ini, L"extend_tracks", 1) != 0);
    meta_.store(read_int(ini, L"show_metadata", 1) != 0);

    const auto s = get();
    fh6::log::info("[fh2] settings: enabled={} station={} mute_dj={} extend_tracks={} "
                   "show_metadata={}",
                   s.enabled, s.takeover_station, s.mute_dj, s.extend_tracks, s.show_metadata);
    return true;
}

Settings SettingsFile::get() const {
    Settings s;
    s.enabled          = enabled_.load();
    s.takeover_station = station_.load();
    s.mute_dj          = mute_dj_.load();
    s.extend_tracks    = extend_.load();
    s.show_metadata    = meta_.load();
    return s;
}

bool StationMap::load(const std::filesystem::path& xml) {
    std::ifstream in{xml, std::ios::binary};
    if (!in) return false;
    std::string line;
    int current = 0;
    while (std::getline(in, line)) {
        if (line.find("<RadioStation ") != std::string::npos) {
            const auto num = attr(line, "Number");
            current        = num.empty() ? 0 : std::atoi(num.c_str());
            stations_.push_back({current, attr(line, "Name")});
            continue;
        }
        if (current && line.find("<Entry ") != std::string::npos) {
            auto sn = attr(line, "SoundName");
            if (!sn.empty()) by_sound_.emplace(std::move(sn), current);
        }
    }
    fh6::log::info("[fh2] radio info: {} stations, {} samples from {}", stations_.size(),
                   by_sound_.size(), xml.string());
    return !stations_.empty();
}

int StationMap::station_for(const std::string& sound_name) const {
    auto it = by_sound_.find(sound_name);
    return it == by_sound_.end() ? 0 : it->second;
}

std::string StationMap::name_of(int number) const {
    for (const auto& s : stations_)
        if (s.number == number) return s.name;
    return "station " + std::to_string(number);
}

std::filesystem::path find_radio_info(const std::filesystem::path& azimuth_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;

    std::vector<fs::path> mounts;
    // Azimuth: data\azimuth.ini [title-1a96545b] folder=FH2 (UTF-16 ini).
    const auto ini = (azimuth_dir / "data" / "azimuth.ini").wstring();
    wchar_t folder[MAX_PATH]{};
    GetPrivateProfileStringW(L"title-1a96545b", L"folder", L"", folder, MAX_PATH, ini.c_str());
    if (folder[0]) {
        fs::path f{folder};
        if (f.is_relative()) f = azimuth_dir / f;
        mounts.push_back(f / "Mount");
        mounts.push_back(f);
    }
    mounts.push_back(azimuth_dir / "FH2" / "Mount");
    mounts.push_back(azimuth_dir); // Mezcal: DLL sits next to the game exe

    for (const auto& m : mounts) {
        for (const char* lang : {"EN", "FR", "DE", "ES", "IT", "BR", "MX", "PL", "RU", "JP"}) {
            auto p = m / "media" / "audio" / (std::string{"RadioInfo_"} + lang + ".xml");
            if (fs::exists(p, ec)) return p;
        }
    }
    return {};
}

} // namespace fh2
