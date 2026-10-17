#include "fh2/radio_hook.hpp"
#include "fh2/offsets.hpp"
#include "fh6/audio_source_manager.hpp"
#include "fh6/log.hpp"
#include "fh6/safe_mem.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace fh2 {

namespace {

using CreateSoundFn = uint32_t (*)(void* system, const char* name, uint32_t mode, void* exinfo,
                                   void** out);
using GetSubSoundFn = uint32_t (*)(void* sound, int index, void** out);
using GetUserDataFn = uint32_t (*)(void* sound, void** out);
using OperatorNewFn = void* (*)(std::size_t);

CreateSoundFn g_create_sound   = nullptr;
GetSubSoundFn g_get_sub_sound  = nullptr;
GetUserDataFn g_get_user_data  = nullptr;
OperatorNewFn g_game_new       = nullptr;

constexpr int kSampleRate   = 48000; // what every source writes into the ring
constexpr int kChannels     = 2;
constexpr int kBytesPerFrame = kChannels * 2;
constexpr uint32_t kDecodeFrames = 2048; // ~43 ms per read callback
constexpr uint32_t kSilentBytes  = kSampleRate * kBytesPerFrame / 4; // 250 ms

// 4 h in ms. Stays below 2^24 so the game's float track clock still counts
// in exact milliseconds.
constexpr int32_t kLongTrackMs = 4 * 60 * 60 * 1000;

// ---- stream identity -------------------------------------------------------
// Each substituted stream carries a pointer into this table as its FMOD
// userdata. The newest track stream's generation is the one the ring feeds;
// anything older (a stream the game is still fading out) gets silence, so two
// streams never split the ring between them.
struct StreamTag {
    std::atomic<uint64_t> gen{0};
};
constexpr std::size_t kTagCount = 256;
StreamTag g_tags[kTagCount];
StreamTag g_silent_tag;
std::atomic<uint64_t> g_next_gen{1};
std::atomic<uint64_t> g_active_gen{0};

bool is_track_tag(const void* ud) noexcept {
    return ud >= static_cast<const void*>(&g_tags[0]) &&
           ud < static_cast<const void*>(&g_tags[kTagCount]);
}
bool is_our_tag(const void* ud) noexcept { return ud == &g_silent_tag || is_track_tag(ud); }

// ---- MSVC std::string slots inside game objects ----------------------------
struct StringHeader {
    std::byte buf[16];
    uint64_t size;
    uint64_t cap;
};
static_assert(sizeof(StringHeader) == 32);

std::string read_str(const std::byte* slot) {
    auto s = fh6::safe_read_msvc_string(slot, 1024);
    return s ? std::move(*s) : std::string{};
}

std::size_t utf8_clip(std::string_view s, std::size_t max) noexcept {
    if (s.size() <= max) return s.size();
    std::size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return n;
}

// Grow the slot's capacity using the game's own allocator, so the game can
// later free or reallocate the buffer itself. GAME THREAD ONLY. The previous
// heap buffer is intentionally leaked: a reader may still be holding it.
bool grow_string(std::byte* slot, uint64_t new_cap) noexcept {
    if (!g_game_new) return false;
    StringHeader hdr{};
    if (!fh6::safe_read(slot, hdr) || hdr.cap < 15 || hdr.size > hdr.cap) return false;
    if (hdr.cap >= new_cap) return true;

    auto* fresh = static_cast<std::byte*>(g_game_new(static_cast<std::size_t>(new_cap + 1)));
    if (!fresh) return false;
    return fh6::seh_call([&] {
        const std::byte* old = hdr.cap > 15 ? *reinterpret_cast<std::byte* const*>(hdr.buf) : slot;
        std::memcpy(fresh, old, static_cast<std::size_t>(hdr.size));
        fresh[hdr.size] = std::byte{0};
        std::memcpy(slot, &fresh, sizeof(fresh));
        std::memset(slot + sizeof(fresh), 0, 16 - sizeof(fresh));
        std::memcpy(slot + 24, &new_cap, sizeof(new_cap));
    });
}

// In-place write, truncating (on a UTF-8 boundary) to the slot's capacity.
bool write_string(std::byte* slot, std::string_view text) noexcept {
    StringHeader hdr{};
    if (!fh6::safe_read(slot, hdr) || hdr.cap < 15 || hdr.size > hdr.cap) return false;
    const std::size_t n = utf8_clip(text, static_cast<std::size_t>(hdr.cap));
    const uint64_t size = n;
    return fh6::seh_call([&] {
        std::byte* dst = hdr.cap > 15 ? *reinterpret_cast<std::byte* const*>(hdr.buf) : slot;
        std::memcpy(dst, text.data(), n);
        dst[n] = std::byte{0};
        std::memcpy(slot + 16, &size, sizeof(size));
    });
}

// ---- per-sample bookkeeping ------------------------------------------------
enum class Kind { other, track, dj, stinger };

constexpr const char* kind_name(Kind k) noexcept {
    switch (k) {
        case Kind::track: return "track";
        case Kind::dj: return "dj";
        case Kind::stinger: return "stinger";
        default: return "other";
    }
}

struct SampleInfo {
    std::byte* props = nullptr;
    Kind kind        = Kind::other;
    int station      = 0;
    std::string sound_name;
    std::string event_name;
    std::string bank;
};

struct Original {
    bool timing = false;
    int32_t length = 0;
    int32_t markers[3]{};
    bool meta = false;
    std::string title;
    std::string artist;
};

std::mutex g_mu; // guards g_originals + metadata cache
std::unordered_map<std::byte*, Original> g_originals;
std::atomic<std::byte*> g_active_props{nullptr};
std::atomic<std::byte*> g_active_rs{nullptr}; // RadioStreamFmod that owns g_active_props
std::byte* g_img_base = nullptr;

// True while `rs` is still a live RadioStreamFmod whose sample is still a
// RadioSample pointing at `props`. Checked before every write made from the
// control thread, so a freed/reused object is never written to.
bool props_still_live(std::byte* rs, std::byte* props) noexcept {
    if (!rs || !props || !g_img_base) return false;
    void* vt = nullptr;
    std::byte* sample = nullptr;
    void* svt = nullptr;
    std::byte* p = nullptr;
    return fh6::safe_read(rs, vt) && vt == g_img_base + rva::kRadioStreamFmodVtbl &&
           fh6::safe_read(rs + layout::kStreamSample, sample) && sample &&
           fh6::safe_read(sample, svt) && svt == g_img_base + rva::kRadioSampleVtbl &&
           fh6::safe_read(sample + layout::kSampleProps, p) && p == props;
}
std::string g_meta_title, g_meta_artist;       // latest from the control loop
std::string g_written_title, g_written_artist; // what's in g_active_props now
std::byte* g_written_props = nullptr;

int station_from_bank(const std::string& bank) noexcept {
    // "...\HZ2_MX_Radio_07_Tracks.bank" / "_07_DJ_EN.bank" / "_07_Stingers_EN.bank"
    auto p = bank.find("_Radio_");
    if (p == std::string::npos) return 0;
    p += 7;
    if (p + 2 > bank.size() || !std::isdigit((unsigned char)bank[p]) ||
        !std::isdigit((unsigned char)bank[p + 1]))
        return 0;
    return (bank[p] - '0') * 10 + (bank[p + 1] - '0');
}

std::string read_cstr(const char* p, std::size_t max = 512) {
    std::string out;
    if (!p) return out;
    fh6::seh_call([&] {
        for (std::size_t i = 0; i < max; ++i) {
            if (!fh6::is_readable(p + i, 1) || !p[i]) break;
            out.push_back(p[i]);
        }
    });
    return out;
}

bool apply_timing(std::byte* props, Original& orig) noexcept {
    int32_t* begin = nullptr;
    int32_t* end   = nullptr;
    if (!fh6::safe_read(props + layout::kPropMarkers, begin) ||
        !fh6::safe_read(props + layout::kPropMarkers + 8, end) || !begin || end < begin ||
        end - begin < layout::kMarkerCount)
        return false;
    return fh6::seh_call([&] {
        auto* len = reinterpret_cast<int32_t*>(props + layout::kPropSampleLength);
        if (!orig.timing) {
            orig.length     = *len;
            orig.markers[0] = begin[layout::kMarkerStingerStart];
            orig.markers[1] = begin[layout::kMarkerDJStart];
            orig.markers[2] = begin[layout::kMarkerEnd];
            orig.timing     = true;
        }
        begin[layout::kMarkerStingerStart] = kLongTrackMs - 2000;
        begin[layout::kMarkerDJStart]      = kLongTrackMs - 1000;
        begin[layout::kMarkerEnd]          = kLongTrackMs;
        *len                               = kLongTrackMs;
    });
}

void restore_timing(std::byte* props, Original& orig) noexcept {
    if (!orig.timing) return;
    int32_t* begin = nullptr;
    int32_t* end   = nullptr;
    if (!fh6::safe_read(props + layout::kPropMarkers, begin) ||
        !fh6::safe_read(props + layout::kPropMarkers + 8, end) || !begin || end < begin ||
        end - begin < layout::kMarkerCount)
        return;
    fh6::seh_call([&] {
        *reinterpret_cast<int32_t*>(props + layout::kPropSampleLength) = orig.length;
        begin[layout::kMarkerStingerStart]                             = orig.markers[0];
        begin[layout::kMarkerDJStart]                                  = orig.markers[1];
        begin[layout::kMarkerEnd]                                      = orig.markers[2];
    });
    orig.timing = false;
}

void restore_meta(std::byte* props, Original& orig) noexcept {
    if (!orig.meta) return;
    write_string(props + layout::kPropDisplayName, orig.title);
    write_string(props + layout::kPropArtist, orig.artist);
    orig.meta = false;
}

void fill_exinfo(std::byte (&ex)[fmod::kExinfoSize], uint32_t length, void* userdata);

} // namespace

// ---- C entry points the patched code / FMOD call ------------------------------
extern "C" {
uint32_t fh2_create_sound_entry(void* system, const char* name, uint32_t mode, void* exinfo,
                                void** out, std::byte* radio_stream) {
    return RadioHook::instance().on_create_sound(system, name, mode, exinfo, out, radio_stream);
}
uint32_t fh2_get_sub_sound_entry(void* sound, int index, void** out) {
    return RadioHook::instance().on_get_sub_sound(sound, index, out);
}
uint32_t fh2_pcm_read(void* sound, void* data, uint32_t len) {
    return RadioHook::instance().on_pcm_read(sound, data, len);
}
uint32_t fh2_pcm_setpos(void*, int, uint32_t, uint32_t) { return fmod::kOk; }
}

namespace {
void fill_exinfo(std::byte (&ex)[fmod::kExinfoSize], uint32_t length, void* userdata) {
    std::memset(ex, 0, sizeof(ex));
    auto put32 = [&](std::ptrdiff_t off, uint32_t v) { std::memcpy(ex + off, &v, 4); };
    auto putp  = [&](std::ptrdiff_t off, const void* v) { std::memcpy(ex + off, &v, 8); };
    put32(fmod::kExCbSize, fmod::kExinfoSize);
    put32(fmod::kExLength, length);
    put32(fmod::kExNumChannels, kChannels);
    put32(fmod::kExFrequency, kSampleRate);
    put32(fmod::kExFormat, static_cast<uint32_t>(fmod::FORMAT_PCM16));
    put32(fmod::kExDecodeBuf, kDecodeFrames);
    putp(fmod::kExPcmRead, reinterpret_cast<const void*>(&fh2_pcm_read));
    putp(fmod::kExPcmSetPos, reinterpret_cast<const void*>(&fh2_pcm_setpos));
    putp(fmod::kExUserData, userdata);
}

// Stub that the createSound call site is pointed at. Forwards the five
// createSound arguments plus the caller's rdi (the RadioStreamFmod*) as a
// sixth argument:
//     sub  rsp, 38h
//     mov  rax, [rsp+60h]        ; caller's 5th arg (Sound**)
//     mov  [rsp+20h], rax
//     mov  [rsp+28h], rdi        ; 6th arg
//     call [rip+5]
//     add  rsp, 38h
//     ret
//     dq   fh2_create_sound_entry
constexpr uint8_t kCreateStub[] = {
    0x48, 0x83, 0xEC, 0x38,                   // sub rsp,38h
    0x48, 0x8B, 0x44, 0x24, 0x60,             // mov rax,[rsp+60h]
    0x48, 0x89, 0x44, 0x24, 0x20,             // mov [rsp+20h],rax
    0x48, 0x89, 0x7C, 0x24, 0x28,             // mov [rsp+28h],rdi
    0xFF, 0x15, 0x05, 0x00, 0x00, 0x00,       // call [rip+5]
    0x48, 0x83, 0xC4, 0x38,                   // add rsp,38h
    0xC3,                                     // ret
};
constexpr std::size_t kCreateStubCode = sizeof(kCreateStub); // 30, target qword follows

// jmp [rip+0]; dq fh2_get_sub_sound_entry
constexpr uint8_t kJmpStub[] = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};

// UNWIND_INFO for kCreateStub: v1, prolog 4 bytes, one code:
// UWOP_ALLOC_SMALL(0x38) at offset 4.
constexpr uint8_t kCreateUnwind[] = {0x01, 0x04, 0x01, 0x00, 0x04, 0x62, 0x00, 0x00};

constexpr uint8_t kOperatorNewPrologue[] = {0x48, 0x89, 0x5C, 0x24, 0x08,
                                            0x57, 0x48, 0x83, 0xEC, 0x20};
std::byte* build_stubs(std::byte* image_base) noexcept {
    std::byte* page = alloc_near(image_base, 0x1000);
    if (!page) return nullptr;
    std::byte* unwind = page + 0x80;
    auto* rtf         = reinterpret_cast<RUNTIME_FUNCTION*>(page + 0xC0);

    std::memcpy(page, kCreateStub, sizeof(kCreateStub));
    const void* create_entry = reinterpret_cast<const void*>(&fh2_create_sound_entry);
    std::memcpy(page + kCreateStubCode, &create_entry, 8);

    std::memcpy((page + 0x40), kJmpStub, sizeof(kJmpStub));
    const void* sub_entry = reinterpret_cast<const void*>(&fh2_get_sub_sound_entry);
    std::memcpy((page + 0x40) + sizeof(kJmpStub), &sub_entry, 8);

    std::memcpy(unwind, kCreateUnwind, sizeof(kCreateUnwind));
    rtf->BeginAddress      = 0;
    rtf->EndAddress        = static_cast<DWORD>(kCreateStubCode);
    rtf->UnwindData        = 0x80;
    if (!RtlAddFunctionTable(rtf, 1, reinterpret_cast<DWORD64>(page)))
        fh6::log::warn("[fh2] RtlAddFunctionTable failed (stack walks through the stub may be off)");

    DWORD old = 0;
    VirtualProtect(page, 0x1000, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), page, 0x1000);

    return page;
}
} // namespace

RadioHook& RadioHook::instance() noexcept {
    static RadioHook h;
    return h;
}

bool RadioHook::install(const GameImage& img, fh6::AudioSourceManager& mgr, SettingsFile& settings,
                        const StationMap& stations) noexcept {
    if (installed()) return true;

    struct Export {
        const char* name;
        uint32_t rva;
    };
    for (const Export& e : {Export{exports::kCreateSound, rva::kCreateSound},
                            Export{exports::kGetSubSound, rva::kGetSubSound},
                            Export{exports::kSoundGetUserData, rva::kSoundGetUserData}}) {
        const uint32_t got = find_export(img, e.name);
        if (got != e.rva) {
            fh6::log::info("[fh2] waiting: export {} at RVA 0x{:X} (expected 0x{:X})", e.name, got,
                           e.rva);
            return false;
        }
    }

    std::byte* create_site = img.at(rva::kTrackCreateSoundCall);
    std::byte* sub_site    = img.at(rva::kTrackGetSubSoundCall);
    if (call_target(create_site) != img.at(rva::kCreateSound) ||
        call_target(sub_site) != img.at(rva::kGetSubSound)) {
        fh6::log::info("[fh2] waiting: radio call sites not in their original state yet");
        return false;
    }
    uint8_t prologue[sizeof(kOperatorNewPrologue)]{};
    const bool new_ok = fh6::safe_read(img.at(rva::kOperatorNew), prologue) &&
                        std::memcmp(prologue, kOperatorNewPrologue, sizeof(prologue)) == 0;

    img_      = img;
    mgr_      = &mgr;
    settings_ = &settings;
    stations_ = &stations;

    g_create_sound  = reinterpret_cast<CreateSoundFn>(img.at(rva::kCreateSound));
    g_get_sub_sound = reinterpret_cast<GetSubSoundFn>(img.at(rva::kGetSubSound));
    g_get_user_data = reinterpret_cast<GetUserDataFn>(img.at(rva::kSoundGetUserData));
    g_game_new      = new_ok ? reinterpret_cast<OperatorNewFn>(img.at(rva::kOperatorNew)) : nullptr;
    if (!new_ok) fh6::log::warn("[fh2] operator new not verified; long titles will be truncated");

    g_img_base = img.base;

    // Stub page within rel32 reach of the game image (built once, even if a
    // later patch step fails and install() is retried).
    if (!stub_page_) stub_page_ = build_stubs(img.base);
    if (!stub_page_) {
        fh6::log::error("[fh2] could not allocate a stub page near the game image");
        return false;
    }
    create_stub_   = stub_page_;
    subsound_stub_ = stub_page_ + 0x40;

    // getSubSound first: it's a no-op for vanilla sounds, so it's safe to
    // have it live before the createSound substitution starts.
    if (!patch_call(sub_site, img.at(rva::kGetSubSound), subsound_stub_)) {
        fh6::log::error("[fh2] failed to patch getSubSound call site");
        return false;
    }
    if (!patch_call(create_site, img.at(rva::kCreateSound), create_stub_)) {
        patch_call(sub_site, subsound_stub_, img.at(rva::kGetSubSound));
        fh6::log::error("[fh2] failed to patch createSound call site");
        return false;
    }

    installed_.store(true, std::memory_order_release);
    fh6::log::info("[fh2] radio hooks installed: image @0x{:X}, stubs @0x{:X}",
                   reinterpret_cast<uintptr_t>(img.base), reinterpret_cast<uintptr_t>(stub_page_));
    return true;
}

void RadioHook::verify() noexcept {
    if (!installed()) return;
    std::byte* create_site = img_.at(rva::kTrackCreateSoundCall);
    std::byte* sub_site    = img_.at(rva::kTrackGetSubSoundCall);
    if (call_target(sub_site) == img_.at(rva::kGetSubSound)) {
        fh6::log::warn("[fh2] getSubSound call site was restored by someone; re-patching");
        patch_call(sub_site, img_.at(rva::kGetSubSound), subsound_stub_);
    }
    if (call_target(create_site) == img_.at(rva::kCreateSound)) {
        fh6::log::warn("[fh2] createSound call site was restored by someone; re-patching");
        patch_call(create_site, img_.at(rva::kCreateSound), create_stub_);
    }
}

void RadioHook::push_metadata(std::string_view title, std::string_view artist) noexcept {
    std::scoped_lock lk{g_mu};
    if (g_meta_title != title) g_meta_title.assign(title);
    if (g_meta_artist != artist) g_meta_artist.assign(artist);

    std::byte* props = g_active_props.load(std::memory_order_acquire);
    if (!props || !settings_ || !settings_->get().show_metadata) return;
    if (!props_still_live(g_active_rs.load(std::memory_order_acquire), props)) {
        g_active_props.store(nullptr, std::memory_order_release);
        return;
    }
    if (props == g_written_props && g_written_title == title && g_written_artist == artist) return;

    const bool ok = write_string(props + layout::kPropDisplayName, title) &&
                    write_string(props + layout::kPropArtist, artist);
    g_written_props = props;
    g_written_title.assign(title);
    g_written_artist.assign(artist);
    if (ok) fh6::log::info(R"([fh2] HUD metadata: "{}" / "{}")", title, artist);
}

uint32_t RadioHook::on_create_sound(void* system, const char* name, uint32_t mode, void* exinfo,
                                    void** out, std::byte* rs) noexcept {
    const auto passthrough = [&] { return g_create_sound(system, name, mode, exinfo, out); };
    if (!settings_ || !mgr_) return passthrough();

    bool created = false; // a substitute stream is already in *out
    try {
        const uint64_t n = stats_.loads_seen.fetch_add(1, std::memory_order_relaxed) + 1;

        SampleInfo info;
        void* vt = nullptr;
        if (rs && fh6::safe_read(rs, vt) && vt == img_.at(rva::kRadioStreamFmodVtbl)) {
            std::byte* sample = nullptr;
            void* svt = nullptr;
            if (fh6::safe_read(rs + layout::kStreamSample, sample) && sample &&
                fh6::safe_read(sample, svt) && svt == img_.at(rva::kRadioSampleVtbl) &&
                fh6::safe_read(sample + layout::kSampleProps, info.props) && info.props) {
                info.sound_name = read_str(info.props + layout::kPropSoundName);
                info.event_name = read_str(info.props + layout::kPropEventName);
            }
        }
        if (!(mode & (fmod::OPENMEMORY | fmod::OPENMEMORY_POINT))) info.bank = read_cstr(name);

        if (info.event_name.find("/Radio/Track") != std::string::npos) info.kind = Kind::track;
        else if (info.event_name.find("/Radio/DJ") != std::string::npos) info.kind = Kind::dj;
        else if (info.event_name.find("/Radio/Stinger") != std::string::npos) info.kind = Kind::stinger;

        info.station = station_from_bank(info.bank);
        if (!info.station && stations_) info.station = stations_->station_for(info.sound_name);
        if (info.kind == Kind::track) stats_.live_station.store(info.station);

        const Settings s    = settings_->get();
        const bool station_ok = s.takeover_station == 0 || info.station == s.takeover_station;
        const bool takeover   = s.enabled && info.props && info.kind != Kind::other && station_ok &&
                              (info.kind == Kind::track || s.mute_dj);

        if (n <= 64 || takeover) {
            fh6::log::info(R"([fh2] load #{}: {} station={} sound="{}" event="{}" bank="{}" -> {})",
                           n, kind_name(info.kind), info.station, info.sound_name, info.event_name,
                           info.bank, takeover ? "TAKEOVER" : "vanilla");
        }

        if (!takeover) {
            if (info.props) {
                if (g_active_props.load() == info.props) g_active_props.store(nullptr);
                std::scoped_lock lk{g_mu};
                if (auto it = g_originals.find(info.props); it != g_originals.end()) {
                    restore_timing(info.props, it->second);
                    restore_meta(info.props, it->second);
                }
            }
            return passthrough();
        }

        std::byte ex[fmod::kExinfoSize];
        const uint32_t keep_dim = mode & (fmod::MODE_2D | fmod::MODE_3D);

        if (info.kind != Kind::track) {
            fill_exinfo(ex, kSilentBytes, &g_silent_tag);
            const uint32_t r = g_create_sound(
                system, nullptr,
                fmod::OPENUSER | fmod::CREATESTREAM | fmod::LOOP_OFF | fmod::NONBLOCKING | keep_dim,
                ex, out);
            if (r == fmod::kOk) {
                created = true;
                stats_.muted.fetch_add(1, std::memory_order_relaxed);
                return r;
            }
            fh6::log::warn("[fh2] silent stream failed (FMOD_RESULT {}); playing original", r);
            return passthrough();
        }

        // Track: a live user stream fed from the ring buffer.
        const uint64_t gen = g_next_gen.fetch_add(1, std::memory_order_relaxed);
        StreamTag* tag     = &g_tags[gen % kTagCount];
        tag->gen.store(gen, std::memory_order_release);
        const uint64_t prev_gen = g_active_gen.exchange(gen, std::memory_order_acq_rel);

        fill_exinfo(ex, 0xFFFFFFFFu, tag);
        const uint32_t r = g_create_sound(
            system, nullptr,
            fmod::OPENUSER | fmod::CREATESTREAM | fmod::LOOP_NORMAL | fmod::NONBLOCKING | keep_dim,
            ex, out);
        if (r != fmod::kOk) {
            uint64_t expected = gen;
            g_active_gen.compare_exchange_strong(expected, prev_gen);
            fh6::log::warn("[fh2] user stream failed (FMOD_RESULT {}); playing original", r);
            return passthrough();
        }
        created = true;

        // The station clock may ask tryStart to seek the event deep into the
        // (now 4 h) track; our stream is live, so always start at 0.
        fh6::seh_call([&] { *reinterpret_cast<int32_t*>(rs + layout::kStreamStartMs) = 0; });

        {
            std::scoped_lock lk{g_mu};
            Original& orig = g_originals[info.props];
            if (s.extend_tracks) {
                if (!apply_timing(info.props, orig))
                    fh6::log::warn("[fh2] couldn't stretch track timing for \"{}\"", info.sound_name);
            } else {
                restore_timing(info.props, orig);
            }

            if (s.show_metadata) {
                std::byte* title_slot  = info.props + layout::kPropDisplayName;
                std::byte* artist_slot = info.props + layout::kPropArtist;
                if (!orig.meta) {
                    orig.title  = read_str(title_slot);
                    orig.artist = read_str(artist_slot);
                    orig.meta   = true;
                }
                // Room for long titles so later in-place updates from the
                // control thread never need an allocation.
                grow_string(title_slot, 191);
                grow_string(artist_slot, 191);
                const std::string title  = g_meta_title.empty() ? "FH2 Universal Radio" : g_meta_title;
                const std::string artist = g_meta_title.empty() ? "" : g_meta_artist;
                write_string(title_slot, title);
                write_string(artist_slot, artist);
                g_written_props  = info.props;
                g_written_title  = title;
                g_written_artist = artist;
                g_active_rs.store(rs, std::memory_order_release);
                g_active_props.store(info.props, std::memory_order_release);
            } else {
                restore_meta(info.props, orig);
                g_active_props.store(nullptr, std::memory_order_release);
            }
        }

        stats_.takeovers.fetch_add(1, std::memory_order_relaxed);
        fh6::log::info("[fh2] station {} ({}) now playing mod audio (stream gen {})", info.station,
                       stations_ ? stations_->name_of(info.station) : std::string{}, gen);
        return r;
    } catch (...) {
        return created ? fmod::kOk : passthrough();
    }
}

uint32_t RadioHook::on_get_sub_sound(void* sound, int index, void** out) noexcept {
    void* ud = nullptr;
    if (sound && g_get_user_data(sound, &ud) == fmod::kOk && is_our_tag(ud)) {
        // A user stream has no subsounds: hand the stream itself to the
        // programmer instrument.
        if (out) *out = sound;
        return fmod::kOk;
    }
    return g_get_sub_sound(sound, index, out);
}

uint32_t RadioHook::on_pcm_read(void* sound, void* data, uint32_t len) noexcept {
    if (!data || !len) return fmod::kOk;
    void* ud = nullptr;
    if (g_get_user_data) g_get_user_data(sound, &ud);

    const bool feed = mgr_ && is_track_tag(ud) &&
                      static_cast<StreamTag*>(ud)->gen.load(std::memory_order_acquire) ==
                          g_active_gen.load(std::memory_order_acquire);
    if (!feed) {
        std::memset(data, 0, len);
        return fmod::kOk;
    }

    stats_.reads.fetch_add(1, std::memory_order_relaxed);
    stats_.last_len.store(len, std::memory_order_relaxed);
    stats_.last_read_ms.store(GetTickCount64(), std::memory_order_relaxed);

    const uint32_t want   = len & ~static_cast<uint32_t>(kBytesPerFrame - 1);
    const std::size_t got = mgr_->ring().read(data, want);

    const float g = std::clamp(gain(), 0.0f, 1.0f);
    if (g < 0.9995f) {
        auto* s = static_cast<int16_t*>(data);
        for (std::size_t i = 0, n = got / 2; i < n; ++i)
            s[i] = static_cast<int16_t>(std::lrintf(static_cast<float>(s[i]) * g));
    }
    if (got < len) {
        std::memset(static_cast<std::byte*>(data) + got, 0, len - got);
        if (got < want) stats_.underruns.fetch_add(1, std::memory_order_relaxed);
    }
    return fmod::kOk;
}

} // namespace fh2
