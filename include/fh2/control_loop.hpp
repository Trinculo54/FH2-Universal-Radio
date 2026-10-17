#pragma once

#include "fh2/settings.hpp"

#include <atomic>
#include <filesystem>
#include <stop_token>
#include <thread>

namespace fh6 {
class AudioSourceManager;
}

namespace fh2 {

// Background thread: waits for Azimuth to map FH2, installs the radio hooks,
// then pumps the active source into the ring, ramps the output gain with the
// source's play state, reports radio audibility (pause menu / radio off) to
// the source, refreshes fh2.ini and pushes now-playing text into the HUD.
class ControlLoop {
public:
    ControlLoop(fh6::AudioSourceManager& mgr, std::filesystem::path data_dir,
                std::filesystem::path azimuth_dir, float configured_gain);
    ~ControlLoop();

    ControlLoop(const ControlLoop&)            = delete;
    ControlLoop& operator=(const ControlLoop&) = delete;

    void set_configured_gain(float g) noexcept { configured_gain_.store(g); }

private:
    void run(const std::stop_token& tok);

    fh6::AudioSourceManager& mgr_;
    std::filesystem::path data_dir_;
    std::filesystem::path azimuth_dir_;
    std::atomic<float> configured_gain_;
    SettingsFile settings_;
    StationMap stations_;
    std::jthread thread_;
};

} // namespace fh2
