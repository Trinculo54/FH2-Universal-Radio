#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace fh2 {

// fh2-radio\fh2.ini -- the FH2-specific knobs. Re-read whenever the file
// changes, so edits apply without restarting the game (on the next track
// the game loads).
struct Settings {
    bool enabled          = true;
    int takeover_station  = 1; // 1..7, 0 = every station
    bool mute_dj          = true;
    bool extend_tracks    = true;
    bool show_metadata    = true;
};

class SettingsFile {
public:
    explicit SettingsFile(std::filesystem::path path);

    // Writes a commented default file if none exists.
    void ensure_exists() const;

    // Re-reads the file if its timestamp changed. Returns true on change.
    bool refresh();

    Settings get() const;

private:
    std::filesystem::path path_;
    std::filesystem::file_time_type stamp_{};
    std::atomic<bool> enabled_{true};
    std::atomic<int> station_{1};
    std::atomic<bool> mute_dj_{true};
    std::atomic<bool> extend_{true};
    std::atomic<bool> meta_{true};
};

// SoundName -> station number, plus the station names, parsed from the
// game's own RadioInfo XML. Used to classify a sample when the bank file
// name isn't available.
struct StationInfo {
    int number = 0;
    std::string name;
};

class StationMap {
public:
    bool load(const std::filesystem::path& radio_info_xml);

    int station_for(const std::string& sound_name) const;
    std::string name_of(int number) const;
    const std::vector<StationInfo>& stations() const { return stations_; }

private:
    std::unordered_map<std::string, int> by_sound_;
    std::vector<StationInfo> stations_;
};

// Locates FH2\Mount\media\audio\RadioInfo_EN.xml (or any language) starting
// from the Azimuth folder; reads data\azimuth.ini for the game folder.
std::filesystem::path find_radio_info(const std::filesystem::path& azimuth_dir);

} // namespace fh2
