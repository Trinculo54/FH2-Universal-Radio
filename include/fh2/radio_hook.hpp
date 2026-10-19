#pragma once

#include "fh2/game_image.hpp"
#include "fh2/settings.hpp"
#include "fh2mod.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace fh6 {
class AudioSourceManager;
}

namespace fh2 {

// Live counters, readable from any thread.
struct RadioStats {
    std::atomic<uint64_t> reads{0};       // PCM read callbacks on the active stream
    std::atomic<uint64_t> underruns{0};   // reads the ring couldn't fully satisfy
    std::atomic<uint32_t> last_len{0};    // bytes asked for by the last read
    std::atomic<uint64_t> last_read_ms{0};
    std::atomic<uint64_t> loads_seen{0};  // radio samples the game loaded
    std::atomic<uint64_t> takeovers{0};   // track streams we substituted
    std::atomic<uint64_t> muted{0};       // DJ / stinger samples silenced
    std::atomic<int> live_station{0};     // station of the last track the game loaded
};

// Owns the two call-site patches inside RadioStreamFmod and everything they
// drive: the substitute FMOD user streams, the PCM feed from the ring
// buffer, the track-length stretch and the HUD metadata.
//
//   RadioStreamFmod::load  -> System::createSound   (patched: substitute a
//                             FMOD_OPENUSER stream for the taken-over station)
//   RadioStreamFmod::start -> Sound::getSubSound    (patched: our stream has
//                             no subsounds, so hand back the stream itself)
//
// The game keeps doing everything else itself -- the Studio event, the radio
// bus, menu ducking, radio volume, pause -- so the audio behaves exactly like
// a native station.
class RadioHook {
public:
    static RadioHook& instance() noexcept;

    // Verifies exports + call-site bytes and patches. False (and nothing
    // patched) if the image doesn't look like the expected build yet; the
    // caller retries -- Azimuth may still be populating the sections.
    bool install(const GameImage& img, fh6::AudioSourceManager& mgr, SettingsFile& settings,
                 const StationMap& stations) noexcept;
    bool installed() const noexcept { return installed_.load(std::memory_order_acquire); }

    // Re-applies a patch if something rewrote the call site. Cheap. (Not
    // needed, and does nothing, when the platform made the patches.)
    void verify() noexcept;
    // True if the patches are platform hooks (platform 2.2+), not our own.
    bool via_platform() const noexcept { return via_platform_; }

    float gain() const noexcept { return gain_.load(std::memory_order_acquire); }
    void set_gain(float g) noexcept { gain_.store(g, std::memory_order_release); }

    // Writes title/artist into the SampleProperties of the track we're
    // currently substituting (in place, no allocation -- the slots are
    // pre-grown on the game thread when the track loads).
    void push_metadata(std::string_view title, std::string_view artist) noexcept;

    RadioStats& stats() noexcept { return stats_; }

    // Throw away up to `bytes` of buffered PCM, as if a speaker had played
    // it. Called while the game isn't reading our stream (menus, another
    // station, loading) so the source -- librespot especially -- keeps
    // running in real time instead of stalling behind a full buffer.
    void discard_realtime(std::size_t bytes) noexcept;

    // ---- called from the patched game code / FMOD threads ----------------
    uint32_t on_create_sound(void* system, const char* name_or_data, uint32_t mode, void* exinfo,
                             void** out_sound) noexcept;
    uint32_t on_get_sub_sound(void* sound, int index, void** out) noexcept;
    uint32_t on_pcm_read(void* sound, void* data, uint32_t len) noexcept;

private:
    RadioHook() = default;

    GameImage img_{};
    fh6::AudioSourceManager* mgr_ = nullptr;
    SettingsFile* settings_       = nullptr;
    const StationMap* stations_   = nullptr;

    std::atomic<bool> installed_{false};
    std::atomic<float> gain_{1.0f};
    RadioStats stats_;

    std::byte* stub_page_       = nullptr;
    bool via_platform_          = false;
    bool platform_refused_      = false;
    FH2Hook* platform_sub_      = nullptr;
    FH2Hook* platform_create_   = nullptr;
    void* platform_orig_sub_    = nullptr; // what the hooks' `original`s are
    void* platform_orig_create_ = nullptr;
    std::byte* create_stub_     = nullptr;
    std::byte* subsound_stub_   = nullptr;
};

} // namespace fh2
