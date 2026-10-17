#pragma once

// FH2 port: the FH6 build injected an FMOD DSP; FH2 substitutes a user
// stream instead (see fh2/radio_hook.hpp). This keeps the small surface the
// HTTP dashboard reads so http_server.cpp stays upstream-identical.

#include "fh2/radio_hook.hpp"

#include <cstdint>

namespace fh6 {
class AudioSourceManager;
}

namespace fh6::fmod_bridge {

enum class DSPMode : uint32_t { off = 0, passthrough = 1, silence = 2, pcm = 3 };

class DSPBridge {
public:
    explicit DSPBridge(AudioSourceManager& mgr) : mgr_{mgr} {}

    DSPMode mode() const noexcept {
        return fh2::RadioHook::instance().installed() ? DSPMode::pcm : DSPMode::off;
    }
    bool attached() const noexcept { return fh2::RadioHook::instance().installed(); }

    float gain() const noexcept { return fh2::RadioHook::instance().gain(); }
    void set_gain(float g) noexcept { fh2::RadioHook::instance().set_gain(g); }
    void set_force_stereo_audio(bool) noexcept {}

    uint64_t underruns() const noexcept {
        return fh2::RadioHook::instance().stats().underruns.load(std::memory_order_relaxed);
    }
    uint64_t call_count() const noexcept {
        return fh2::RadioHook::instance().stats().reads.load(std::memory_order_relaxed);
    }
    uint32_t last_buffer_len() const noexcept {
        return fh2::RadioHook::instance().stats().last_len.load(std::memory_order_relaxed) / 4;
    }
    uint32_t last_out_channels() const noexcept { return 2; }

    AudioSourceManager& manager() noexcept { return mgr_; }

private:
    AudioSourceManager& mgr_;
};

} // namespace fh6::fmod_bridge
