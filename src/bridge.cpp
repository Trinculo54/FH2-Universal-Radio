// Bridge entry point. Wires up config, sources, the FH2 radio hooks, and the
// HTTP dashboard, then parks the thread the DLL spawned us on.
//
// FH2 port of FH6 Universal Radio (GPLv3, github.com/g0ldyy/fh6-universal-radio).

#include "fh6/log.hpp"
#include "fh6/config.hpp"
#include "fh6/config_store.hpp"
#include "fh6/deps.hpp"
#include "fh6/audio_source_manager.hpp"
#include "fh6/fmod/dsp_bridge.hpp"
#include "fh2/control_loop.hpp"
#include "fh6/http/http_server.hpp"
#include "fh6/sources/local_file_source.hpp"
#include "fh6/sources/external_audio_source.hpp"
#include "fh6/sources/youtube_music_source.hpp"
#include "fh6/sources/jellyfin_source.hpp"
#include "fh6/sources/spotify_source.hpp"
#include "fh6/worker/worker_client.hpp"
#include "fh6/sources/online_radio_source.hpp"
#include "fh6/subprocess.hpp"

#include <windows.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace fh6 {

namespace {

SpotifyConfig anchor_spotify(SpotifyConfig sp, const std::filesystem::path& data_dir) {
    if (!sp.cache_dir.empty() && sp.cache_dir.is_relative()) sp.cache_dir = data_dir / sp.cache_dir;
    return sp;
}

// Swap blank binary paths for the auto-downloaded copies. Returned to sources
// only -- the stored config keeps the user's blanks so the dashboard still
// shows "auto".
Config with_resolved_bins(Config c, const DependencyManager& deps) {
    c.general.ffmpeg_path       = deps.resolve(Tool::ffmpeg, c.general.ffmpeg_path);
    c.youtube_music.yt_dlp_path = deps.resolve(Tool::yt_dlp, c.youtube_music.yt_dlp_path);
    c.spotify.librespot_path    = deps.resolve(Tool::librespot, c.spotify.librespot_path);

    log::info("[bridge] Resolved ffmpeg path to: {}", c.general.ffmpeg_path.string());
    log::info("[bridge] Resolved yt-dlp path to: {}", c.youtube_music.yt_dlp_path.string());
    log::info("[bridge] Resolved librespot path to: {}", c.spotify.librespot_path.string());
    return c;
}

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in{p, std::ios::binary};
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return std::move(ss).str();
}

// Refuse to start if the bundled webui has been stripped of credits or
// donation links. Keeps forks honest about the GPLv3 attribution requirement
// and stops the project from being re-skinned with the funding links removed.
bool verify_ui_credits(const std::filesystem::path& ui_dir) {
    const auto index = ui_dir / "index.html";
    const auto html  = slurp(index);
    if (html.empty()) {
        log::error("[bridge] webui index.html missing or unreadable at {}", index.string());
        return false;
    }
    constexpr std::array<std::string_view, 5> required = {
        "g0ldyy",                                // author attribution
        "GPLv3",                                 // license credit
        "github.com/sponsors/g0ldyy",            // GitHub Sponsors link
        "ko-fi.com/g0ldyy",                      // Ko-fi link
        "github.com/g0ldyy/fh6-universal-radio", // upstream repo link
    };
    for (auto needle : required) {
        if (html.find(needle) == std::string::npos) {
            log::error("[bridge] webui is missing required credit/donation marker '{}' -- "
                       "refusing to start. See LICENSE (GPLv3) for attribution requirements.",
                       std::string{needle});
            return false;
        }
    }
    return true;
}

} // namespace

// The folders come from FH2ML (src/proxy/dll_main.cpp): azimuth_dir has
// azimuth.exe, mod_dir is the installed mod (mods\universalradio: ui\, the
// worker, config.example.toml; read-only, replaced on every update), data_dir
// is the player's (FH2ML\data\universalradio: fh2.ini, config.toml, logs, the
// Spotify login, downloaded helpers; kept across updates).
void run_bridge(const std::filesystem::path& azimuth_dir, const std::filesystem::path& mod_dir,
                const std::filesystem::path& data_dir) noexcept {
    if (HANDLE guard = CreateMutexW(nullptr, TRUE, L"Local\\fh2-universal-radio-bridge");
        guard && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(guard);
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(data_dir, ec);

    log::init(data_dir / "bridge.log");
    subprocess::set_log_directory(data_dir);
    log::info("[bridge] FH2 Universal Radio {} starting; mod_dir={} data_dir={}", FH2_RADIO_VERSION,
              mod_dir.string(), data_dir.string());

    // Update notices come from FH2ML (the descriptor in proxy/dll_main.cpp).

    const auto ui_dir = mod_dir / "ui";
    if (!verify_ui_credits(ui_dir)) {
        log::error("[bridge] aborting startup: webui credits/donation links check failed");
        return;
    }

    // First launch: start from the example config the mod ships with.
    const auto config_file = data_dir / "config.toml";
    if (!std::filesystem::exists(config_file, ec) &&
        std::filesystem::copy_file(mod_dir / "config.example.toml", config_file, ec))
        log::info("[bridge] config.toml seeded from config.example.toml");

    ConfigStore store{config_file, load_config(config_file)};
    auto cfg = store.snapshot();

    const std::size_t ring_bytes = static_cast<std::size_t>(cfg.general.ring_buffer_mb) << 20;
    AudioSourceManager mgr{ring_bytes};

    DependencyManager deps{data_dir / "bin"};

    // Worker process: delegates CreateProcess calls to a small external exe
    // so the fork() Wine performs is cheap (~5 MB) instead of copying the
    // game's multi-GB page table.  Falls back to direct spawn if absent.
    auto worker = std::make_shared<worker::WorkerClient>();
    {
        auto worker_exe = mod_dir / "fh2-radio-worker.exe";

        if (worker->start(worker_exe, {{L"RUST_LOG", L"librespot=info,librespot_playback::player=debug,librespot_metadata=trace"},
                                       {L"FH2_RADIO_DATA_DIR", data_dir.wstring()}})) {
            log::info("[bridge] worker process started");
        } else {
            log::warn("[bridge] worker process unavailable -- falling back to direct spawn");
        }
    }

    // Register/unregister sources to match the enabled flags. Called at
    // startup and on every config change so toggling enabled adds/removes
    // the dashboard tile live, without a game restart.
    auto sync_sources = [&mgr, &data_dir, &worker](const Config& c) {
        if (c.local_files.enabled && !mgr.find("local_files")) {
            auto src = std::make_unique<sources::LocalFileSource>(
                c.local_files, c.general.ffmpeg_path, data_dir / "local_index.json", worker.get());
            if (src->initialize()) mgr.register_source(std::move(src));
        } else if (!c.local_files.enabled && mgr.find("local_files")) {
            mgr.unregister_source("local_files");
        }
        if (c.youtube_music.enabled && !mgr.find("youtube_music")) {
            auto src = std::make_unique<sources::YouTubeMusicSource>(c.youtube_music,
                                                                     c.general.ffmpeg_path,
                                                                     worker.get());
            if (src->initialize()) mgr.register_source(std::move(src));
        } else if (!c.youtube_music.enabled && mgr.find("youtube_music")) {
            mgr.unregister_source("youtube_music");
        }
        if (c.jellyfin.enabled && !mgr.find("jellyfin")) {
            auto src = std::make_unique<sources::JellyfinSource>(c.jellyfin, c.general.ffmpeg_path,
                                                                  worker.get());
            if (src->initialize()) mgr.register_source(std::move(src));
        } else if (!c.jellyfin.enabled && mgr.find("jellyfin")) {
            mgr.unregister_source("jellyfin");
        }
        if (c.online_radio.enabled && !mgr.find("online_radio")) {
            auto src = std::make_unique<sources::OnlineRadioSource>(c.online_radio,
                                                                    c.general.ffmpeg_path, worker.get());
            if (src->initialize()) mgr.register_source(std::move(src));
        } else if (!c.online_radio.enabled && mgr.find("online_radio")) {
            mgr.unregister_source("online_radio");
        }

        if (c.external_audio.enabled && !mgr.find("external_audio")) {
            auto src = std::make_unique<sources::ExternalAudioSource>(c.external_audio);
            if (src->initialize()) mgr.register_source(std::move(src));
        } else if (!c.external_audio.enabled && mgr.find("external_audio")) {
            mgr.unregister_source("external_audio");
        }
        if (c.spotify.enabled && !mgr.find("spotify")) {
            auto src = std::make_unique<sources::SpotifySource>(anchor_spotify(c.spotify, data_dir),
                                                                c.general.ffmpeg_path, worker.get());
            if (src->initialize()) mgr.register_source(std::move(src));
        } else if (!c.spotify.enabled && mgr.find("spotify")) {
            mgr.unregister_source("spotify");
        }
    };

    sync_sources(with_resolved_bins(cfg, deps));

    if (!mgr.switch_to(cfg.general.default_source) && !mgr.switch_to(cfg.general.fallback_source)) {
        auto snap = mgr.sources_snapshot();
        if (snap.empty()) {
            log::warn("[bridge] no sources registered");
        } else if (snap.size() == 1) {
            if (!mgr.switch_to(snap[0]->name())) {
                log::error("[bridge] failed to switch to sole registered source '{}'",
                           snap[0]->name());
            }
        } else {
            log::warn("[bridge] configured default/fallback sources not found among {} registered "
                      "sources",
                      snap.size());
        }
    }

    fmod_bridge::DSPBridge bridge{mgr};
    bridge.set_gain(cfg.audio.output_gain);

    // Waits for Azimuth to map FH2, patches the radio, then pumps audio.
    auto ctrl = std::make_unique<fh2::ControlLoop>(mgr, data_dir, azimuth_dir, cfg.audio.output_gain);

    for (auto* s : mgr.sources_snapshot()) s->set_playback_options(cfg.playback);

    auto apply_config = [&bridge, &mgr, &data_dir, &deps, sync_sources,
                         ctrl_ptr = ctrl.get()](const Config& raw) {
        const Config c = with_resolved_bins(raw, deps);
        sync_sources(c);
        if (!mgr.active()) {
            if (!mgr.switch_to(c.general.default_source) &&
                !mgr.switch_to(c.general.fallback_source)) {
                auto snap = mgr.sources_snapshot();
                if (snap.size() == 1) {
                    if (!mgr.switch_to(snap[0]->name())) {
                        log::error("[bridge] failed to switch to sole registered source '{}'",
                                   snap[0]->name());
                    }
                }
            }
        }

        // Push the gain to both: the control loop's ramper otherwise snaps
        // the bridge value back to its own cached target on the next tick.
        bridge.set_gain(c.audio.output_gain);
        if (ctrl_ptr) ctrl_ptr->set_configured_gain(c.audio.output_gain);
        if (auto* local = dynamic_cast<sources::LocalFileSource*>(mgr.find("local_files"))) {
            local->set_ffmpeg_path(c.general.ffmpeg_path);
            local->set_config(c.local_files);
            if (mgr.active() == local && local->track_count() > 0 &&
                local->playback_state() != PlaybackState::playing) {
                local->play();
            }
        }
        if (auto* yt = dynamic_cast<sources::YouTubeMusicSource*>(mgr.find("youtube_music"))) {
            yt->set_ffmpeg_path(c.general.ffmpeg_path);
            yt->set_config(c.youtube_music); 
            yt->set_yt_dlp_path(c.youtube_music.yt_dlp_path);
            yt->set_shuffle(c.youtube_music.shuffle);
            if (mgr.active() == yt && yt->playback_state() != PlaybackState::playing) {
                yt->play();
            }
        }
        if (auto* jf = dynamic_cast<sources::JellyfinSource*>(mgr.find("jellyfin"))) {
            jf->set_ffmpeg_path(c.general.ffmpeg_path);
            jf->set_config(c.jellyfin);
            if (mgr.active() == jf && jf->playback_state() != PlaybackState::playing) {
                jf->play();
            }
        }
        if (auto* ext = dynamic_cast<sources::ExternalAudioSource*>(mgr.find("external_audio"))) {
            ext->set_config(c.external_audio);
        }
        if (auto* sp = dynamic_cast<sources::SpotifySource*>(mgr.find("spotify"))) {
            sp->set_config(anchor_spotify(c.spotify, data_dir), c.general.ffmpeg_path);
        }
        if (auto* rd = dynamic_cast<sources::OnlineRadioSource*>(mgr.find("online_radio"))) {
            rd->set_ffmpeg_path(c.general.ffmpeg_path);
            rd->set_config(c.online_radio);
            if (mgr.active() == rd && rd->playback_state() != PlaybackState::playing) {
                rd->play();
            }
        }

        for (auto* s : mgr.sources_snapshot()) s->set_playback_options(c.playback);
    };
    store.on_change(apply_config);

    // Re-resolve binary paths into the live sources once a download lands.
    deps.start([&store, &apply_config] { apply_config(store.snapshot()); });

    http::HttpServer http{mgr, bridge, store, cfg.general.port, ui_dir, deps};
    log::info("[bridge] running on port {}", cfg.general.port);

    for (;;) Sleep(60'000);
}

} // namespace fh6