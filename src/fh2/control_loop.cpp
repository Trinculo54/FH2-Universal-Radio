#include "fh2/control_loop.hpp"
#include "fh2/now_playing.hpp"
#include "fh2/platform_link.hpp"
#include "fh2/game_image.hpp"
#include "fh2/radio_hook.hpp"
#include "fh6/audio_source.hpp"
#include "fh6/audio_source_manager.hpp"
#include "fh6/log.hpp"

#include <windows.h>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace fh2 {

namespace {
using namespace std::chrono_literals;
constexpr auto kTick = 20ms;

// Frozen-read ticks (while the source is producing) before we decide the
// game silenced the station (pause menu, radio off, other station) and let
// the source mirror that onto any live player it wraps. 3.5 s @ 20 ms.
constexpr int kInaudibleTicks = 175;
constexpr int kMetaEveryTicks = 25;     // 500 ms
constexpr int kHousekeepTicks = 50;     // 1 s
constexpr auto kImagePoll     = 250ms;

// After this long without the game reading our stream, keep the source
// moving at real-time speed by discarding audio (see discard_realtime).
constexpr auto kDrainAfter = 1s;
constexpr std::size_t kBytesPerMs = 48000 * 4 / 1000; // 48 kHz S16 stereo
} // namespace

ControlLoop::ControlLoop(fh6::AudioSourceManager& mgr, std::filesystem::path data_dir,
                         std::filesystem::path azimuth_dir, float configured_gain)
    : mgr_{mgr}, data_dir_{std::move(data_dir)}, azimuth_dir_{std::move(azimuth_dir)},
      configured_gain_{configured_gain}, settings_{data_dir_ / "fh2.ini"},
      thread_{[this](const std::stop_token& tok) { run(tok); }} {}

ControlLoop::~ControlLoop() {
    thread_.request_stop();
    if (thread_.joinable()) thread_.join();
}

void ControlLoop::run(const std::stop_token& tok) {
    settings_.ensure_exists();
    settings_.refresh();

    if (auto xml = find_radio_info(azimuth_dir_); !xml.empty()) {
        stations_.load(xml);
    } else {
        fh6::log::warn("[fh2] RadioInfo_XX.xml not found; station detection uses bank names only");
    }

    auto& hook = RadioHook::instance();

    // ---- phase 1: wait for the translation layer to map FH2, then patch ----
    // fh2mod_init runs a moment after we're loaded; with platform 2.2+ the
    // radio's patches are platform hooks.
    if (const FH2ModApi* api = wait_for_platform(5000))
        fh6::log::info("[fh2] FH2 Mod Platform {}{}", api->platform_version,
                       platform_v3() ? "; the radio hooks go through it" : "");
    fh6::log::info("[fh2] waiting for Forza Horizon 2 to start...");
    GameImage img{};
    int attempts = 0;
    auto next_try  = std::chrono::steady_clock::now();
    auto last_tick = next_try;
    while (!tok.stop_requested() && !hook.installed()) {
        // Nothing reads the ring until the hooks are in, so pump and discard
        // at real-time speed: a Spotify session connected from the launcher
        // keeps playing instead of sitting frozen.
        const auto now = std::chrono::steady_clock::now();
        const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick).count();
        last_tick      = now;
        mgr_.pump_once();
        {
            std::byte scratch[4096];
            std::size_t bytes = static_cast<std::size_t>(std::clamp<long long>(ms, 0, 200)) * kBytesPerMs;
            while (bytes) {
                const std::size_t n = mgr_.ring().read(scratch, std::min(bytes, sizeof(scratch)));
                if (!n) break;
                bytes -= n;
            }
        }

        if (now >= next_try) {
            next_try = now + (attempts >= 240 ? std::chrono::milliseconds{5s} : std::chrono::milliseconds{kImagePoll});
            if (!img.valid()) {
                if (auto found = find_game_image()) {
                    img = *found;
                    fh6::log::info("[fh2] found FH2 image @0x{:X}", reinterpret_cast<uintptr_t>(img.base));
                }
            }
            if (img.valid()) {
                // Retry quietly: the sections may still be getting copied in.
                if (hook.install(img, mgr_, settings_, stations_)) break;
                if (++attempts == 240) {
                    fh6::log::error("[fh2] gave up patching after 60 s -- is this the supported FH2 "
                                    "build (forza_durango_release_final.exe, 2015-11-24)?");
                }
            }
        }
        std::this_thread::sleep_for(kTick);
    }
    if (tok.stop_requested()) return;

    // ---- phase 2: steady state ---------------------------------------------
    auto& stats            = hook.stats();
    uint64_t prev_reads    = stats.reads.load();
    int idle_ticks         = 0;
    bool audible           = true;
    bool primed            = false;
    fh6::IAudioSource* tracked = nullptr;
    int tick               = 0;
    auto last_read_change  = std::chrono::steady_clock::now();
    auto last_tick_time    = last_read_change;
    bool draining          = false;

    auto next = std::chrono::steady_clock::now();
    while (!tok.stop_requested()) {
        const auto now = std::chrono::steady_clock::now();
        if (next < now) next = now;
        next += kTick;
        ++tick;

        mgr_.pump_once();

        if (tick % kHousekeepTicks == 0) {
            settings_.refresh();
            hook.verify();
        }

        auto* active    = mgr_.active();
        const auto st   = active ? active->playback_state() : fh6::PlaybackState::stopped;
        const bool busy = st == fh6::PlaybackState::playing || st == fh6::PlaybackState::buffering;
        const uint64_t reads = stats.reads.load(std::memory_order_relaxed);

        // Audibility edges for sources wrapping a live player.
        if (active && busy) {
            if (active != tracked) {
                tracked    = active;
                idle_ticks = 0;
                primed     = false;
                audible    = true;
            }
            if (reads != prev_reads) {
                idle_ticks = 0;
                primed     = true;
            } else if (primed) {
                ++idle_ticks;
            }
            const bool now_audible = idle_ticks < kInaudibleTicks;
            if (now_audible != audible) {
                audible = now_audible;
                fh6::log::info("[fh2] radio {} -- telling '{}'", audible ? "audible" : "silent",
                               active->name());
                active->on_radio_audible(audible);
            }
        } else {
            tracked    = nullptr;
            idle_ticks = 0;
            primed     = false;
            audible    = true;
        }
        // Real-time drain while the game isn't listening. librespot's pipe
        // output has no clock of its own -- it plays exactly as fast as we
        // read -- so without this it freezes mid-song whenever the radio is
        // paused or on another station, and Spotify drops the device.
        if (reads != prev_reads) last_read_change = now;
        const bool idle = now - last_read_change >= kDrainAfter;
        if (idle != draining) {
            draining = idle;
            fh6::log::info("[fh2] game {} reading the radio stream -- {}",
                           idle ? "stopped" : "resumed",
                           idle ? "keeping the source running in real time" : "normal playback");
        }
        if (idle) {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick_time).count();
            hook.discard_realtime(static_cast<std::size_t>(std::clamp<long long>(ms, 0, 200)) * kBytesPerMs);
        }
        last_tick_time = now;
        prev_reads = reads;

        // Now-playing text for the radio HUD.
        if (tick % kMetaEveryTicks == 0 && active) {
            const auto t = active->current_track();
            std::string title  = t.title;
            std::string artist = t.artist;
            set_now_playing(busy && !draining, std::string{active->display_name()}, t.title, t.artist);
            if (title.empty()) {
                title  = std::string{active->display_name()};
                artist = "FH2 Universal Radio";
            }
            hook.push_metadata(title, artist);
        } else if (tick % kMetaEveryTicks == 0) {
            set_now_playing(false, {}, {}, {});
        }

        // 1-pole ramp (~100 ms) so play/pause fades instead of clicking.
        const float target = busy ? configured_gain_.load() : 0.0f;
        const float cur    = hook.gain();
        float g            = cur + (target - cur) * 0.1f;
        if (std::abs(g - cur) < 1e-4f) g = target;
        hook.set_gain(g);

        std::this_thread::sleep_until(next);
    }
}

} // namespace fh2
