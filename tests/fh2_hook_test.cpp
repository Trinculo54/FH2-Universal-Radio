// Offline test for the FH2 radio hook (runs under Wine or Windows).
//
// Maps the real forza_durango_release_final.exe the way a translation layer
// does (sections + relocations into one private allocation), installs the
// hooks against it, swaps the FMOD entry points in the *mapped image* for
// fakes, then drives the patched code path with a fake RadioStreamFmod /
// SampleProperties graph and checks every decision the hook makes.
//
//   fh2_hook_test.exe <path to forza_durango_release_final.exe> [RadioInfo_EN.xml]
//
// With FH2_TEST_PLATFORM=1 it runs as azimuth.exe next to the real FH2 Mod
// Platform (d3dcompiler_47.dll) and fh2-mods\RadioShim.dll, which hands it
// the platform's API, so the call sites are patched as platform hooks and
// every game call goes through the platform's stubs (tests/run_platform_hook_test.sh).

#include "fh2/game_image.hpp"
#include "fh2/offsets.hpp"
#include "fh2/platform_link.hpp"
#include "fh2/radio_hook.hpp"
#include "fh2/settings.hpp"
#include "fh6/audio_source_manager.hpp"
#include "fh6/log.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace fh2;

static int g_fail = 0;
#define CHECK(c)                                                                                  \
    do {                                                                                          \
        if (!(c)) {                                                                               \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                              \
            ++g_fail;                                                                             \
        } else {                                                                                  \
            std::printf("ok   %s\n", #c);                                                         \
        }                                                                                         \
    } while (0)

// ---- map the PE like Azimuth --------------------------------------------------
static std::byte* map_image(const char* path) {
    std::ifstream in{path, std::ios::binary};
    std::vector<char> file((std::istreambuf_iterator<char>(in)), {});
    if (file.size() < 4096) return nullptr;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(file.data());
    auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    const auto size = nt->OptionalHeader.SizeOfImage;
    auto* base = static_cast<std::byte*>(
        VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    std::memcpy(base, file.data(), nt->OptionalHeader.SizeOfHeaders);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (sec->SizeOfRawData)
            std::memcpy(base + sec->VirtualAddress, file.data() + sec->PointerToRawData,
                        std::min<DWORD>(sec->SizeOfRawData, sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData));
    }
    // relocations
    const auto delta = reinterpret_cast<uint64_t>(base) - nt->OptionalHeader.ImageBase;
    auto& rd         = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    for (auto* blk = reinterpret_cast<IMAGE_BASE_RELOCATION*>(base + rd.VirtualAddress);
         reinterpret_cast<std::byte*>(blk) < base + rd.VirtualAddress + rd.Size && blk->SizeOfBlock;
         blk = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<std::byte*>(blk) + blk->SizeOfBlock)) {
        auto* e = reinterpret_cast<uint16_t*>(blk + 1);
        for (DWORD i = 0; i < (blk->SizeOfBlock - sizeof(*blk)) / 2; ++i)
            if ((e[i] >> 12) == IMAGE_REL_BASED_DIR64)
                *reinterpret_cast<uint64_t*>(base + blk->VirtualAddress + (e[i] & 0xFFF)) += delta;
    }
    DWORD old;
    VirtualProtect(base, size, PAGE_EXECUTE_READ, &old); // like a real mapping
    return base;
}

static void write_jmp(std::byte* at, const void* to) {
    DWORD old;
    VirtualProtect(at, 14, PAGE_EXECUTE_READWRITE, &old);
    const uint8_t j[6] = {0xFF, 0x25, 0, 0, 0, 0};
    std::memcpy(at, j, 6);
    std::memcpy(at + 6, &to, 8);
    VirtualProtect(at, 14, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, 14);
}

// ---- fake FMOD ----------------------------------------------------------------
struct FakeSound {
    uint32_t magic = 0x534F554E;
    void* userdata = nullptr;
    void* pcmread  = nullptr;
    uint32_t mode  = 0;
    uint32_t length = 0;
    int freq = 0, channels = 0, format = 0;
    bool from_bank = false;
};
struct CreateCall {
    void* system;
    std::string name;
    bool name_null;
    uint32_t mode;
    FakeSound* sound;
};
static std::vector<CreateCall> g_calls;
static FakeSound g_bank_sub;

static uint32_t fake_create_sound(void* system, const char* name, uint32_t mode, void* exinfo,
                                  void** out) {
    auto* s = new FakeSound;
    auto* ex = static_cast<std::byte*>(exinfo);
    s->mode = mode;
    if (ex) {
        std::memcpy(&s->length, ex + fmod::kExLength, 4);
        std::memcpy(&s->channels, ex + fmod::kExNumChannels, 4);
        std::memcpy(&s->freq, ex + fmod::kExFrequency, 4);
        std::memcpy(&s->format, ex + fmod::kExFormat, 4);
        std::memcpy(&s->pcmread, ex + fmod::kExPcmRead, 8);
        std::memcpy(&s->userdata, ex + fmod::kExUserData, 8);
    }
    s->from_bank = !(mode & fmod::OPENUSER);
    g_calls.push_back({system, name ? name : "", name == nullptr, mode, s});
    *out = s;
    return 0;
}
static uint32_t fake_get_sub_sound(void*, int, void** out) {
    *out = &g_bank_sub;
    return 0;
}
static uint32_t fake_get_user_data(void* sound, void** out) {
    auto* s = static_cast<FakeSound*>(sound);
    *out    = s && s->magic == 0x534F554E ? s->userdata : nullptr;
    return 0;
}
static void* fake_new(std::size_t n) { return std::malloc(n); }

// ---- fake game objects --------------------------------------------------------
struct MsvcString {
    char buf[16];
    uint64_t size, cap;
};
static void set_str(std::byte* slot, const std::string& s) {
    auto* m = reinterpret_cast<MsvcString*>(slot);
    std::memset(m, 0, sizeof(*m));
    if (s.size() < 16) {
        std::memcpy(m->buf, s.c_str(), s.size() + 1);
        m->cap = 15;
    } else {
        char* p = static_cast<char*>(std::malloc(s.size() + 1));
        std::memcpy(p, s.c_str(), s.size() + 1);
        std::memcpy(m->buf, &p, 8);
        m->cap = s.size();
    }
    m->size = s.size();
}
static std::string get_str(const std::byte* slot) {
    auto* m = reinterpret_cast<const MsvcString*>(slot);
    const char* p = m->cap > 15 ? *reinterpret_cast<char* const*>(m->buf) : m->buf;
    return std::string(p, m->size);
}

struct FakeSample {
    std::byte* props;
    std::byte rs[0x60]{};
    std::byte sample[0x20]{};
    int32_t markers[8];
};
static FakeSample* make_sample(GameImage img, const std::string& sound, const std::string& event,
                               const std::string& title, const std::string& artist, int32_t len) {
    auto* f  = new FakeSample;
    f->props = static_cast<std::byte*>(std::calloc(1, 0x100));
    set_str(f->props + layout::kPropSoundName, sound);
    set_str(f->props + layout::kPropEventName, event);
    set_str(f->props + layout::kPropDisplayName, title);
    set_str(f->props + layout::kPropArtist, artist);
    *reinterpret_cast<int32_t*>(f->props + layout::kPropSampleLength) = len;
    int32_t m[8] = {0, 227, 50000, 50000, 218905, len - 1300, len - 300, len - 4};
    std::memcpy(f->markers, m, sizeof(m));
    int32_t* b = f->markers;
    int32_t* e = f->markers + 8;
    std::memcpy(f->props + layout::kPropMarkers, &b, 8);
    std::memcpy(f->props + layout::kPropMarkers + 8, &e, 8);
    std::memcpy(f->props + layout::kPropMarkers + 16, &e, 8);
    void* vt = img.at(rva::kRadioStreamFmodVtbl);
    std::memcpy(f->rs, &vt, 8);
    void* svt = img.at(event.find("/DJ") != std::string::npos ? rva::kDJSampleVtbl : rva::kMusicSampleVtbl);
    std::memcpy(f->sample, &svt, 8);
    int32_t start = 123456;
    std::memcpy(f->rs + layout::kStreamStartMs, &start, 4);
    std::byte* sp = f->sample;
    std::memcpy(f->rs + layout::kStreamSample, &sp, 8);
    std::memcpy(f->sample + layout::kSampleProps, &f->props, 8);
    return f;
}

// Calls `stub` the way the game's call site does: args in rcx/rdx/r8/r9/[rsp+20h],
// with rdi = RadioStreamFmod*.
using ThunkFn = uint32_t (*)();
static uint32_t call_like_game(std::byte* stub, std::byte* rs, void* system, const char* name,
                               uint32_t mode, void* exinfo, void** out) {
    static std::byte* code = static_cast<std::byte*>(
        VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    std::byte* p = code;
    auto emit = [&](std::initializer_list<uint8_t> b) { for (auto x : b) *p++ = std::byte{x}; };
    auto imm64 = [&](const void* v) { std::memcpy(p, &v, 8); p += 8; };
    emit({0x57});                         // push rdi
    emit({0x48, 0x83, 0xEC, 0x30});       // sub rsp,30h
    emit({0x48, 0xBF}); imm64(rs);        // mov rdi, rs
    emit({0x48, 0xB9}); imm64(system);    // mov rcx, system
    emit({0x48, 0xBA}); imm64(name);      // mov rdx, name
    emit({0x41, 0xB8}); std::memcpy(p, &mode, 4); p += 4; // mov r8d, mode
    emit({0x49, 0xB9}); imm64(exinfo);    // mov r9, exinfo
    emit({0x48, 0xB8}); imm64(out);       // mov rax, out
    emit({0x48, 0x89, 0x44, 0x24, 0x20}); // mov [rsp+20h], rax
    emit({0x48, 0xB8}); imm64(stub);      // mov rax, stub
    emit({0xFF, 0xD0});                   // call rax
    emit({0x48, 0x83, 0xC4, 0x30});       // add rsp,30h
    emit({0x5F});                         // pop rdi (also checks the stub preserved rdi)
    emit({0xC3});
    FlushInstructionCache(GetCurrentProcess(), code, 4096);
    return reinterpret_cast<ThunkFn>(code)();
}

using PcmRead = uint32_t (*)(void*, void*, uint32_t);

extern "C" __declspec(dllexport) void fh2_test_set_platform(const FH2ModApi* api, FH2Mod* mod) {
    set_platform(api, mod);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: fh2_hook_test <forza_durango_release_final.exe>\n");
        return 2;
    }
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    std::string dir = std::string{tmp} + "fh2_hook_test";
    CreateDirectoryA(dir.c_str(), nullptr);
    fh6::log::init(dir + "\\test.log");

    char env[8]{};
    const bool with_platform = GetEnvironmentVariableA("FH2_TEST_PLATFORM", env, sizeof env) && env[0] == '1';
    if (with_platform) CHECK(LoadLibraryW(L"d3dcompiler_47.dll") != nullptr); // as azimuth.exe does

    std::byte* mapped = map_image(argv[1]);
    CHECK(mapped != nullptr);

    auto found = find_game_image();
    CHECK(found.has_value());
    CHECK(found && found->base == mapped);
    GameImage img = *found;

    CHECK(find_export(img, exports::kCreateSound) == rva::kCreateSound);
    CHECK(find_export(img, exports::kGetSubSound) == rva::kGetSubSound);
    CHECK(find_export(img, exports::kSoundGetUserData) == rva::kSoundGetUserData);

    // Settings + station map.
    std::string ini = dir + "\\fh2.ini";
    DeleteFileA(ini.c_str());
    SettingsFile settings{ini};
    settings.ensure_exists();
    CHECK(settings.refresh());
    CHECK(settings.get().takeover_station == 1);
    // The scenarios below take over Innovative Leisure (7).
    Sleep(1100);
    {
        std::ofstream f{ini, std::ios::trunc};
        f << "[takeover]\nenabled=1\nstation=7\n";
    }
    CHECK(settings.refresh() && settings.get().takeover_station == 7);
    StationMap stations;
    if (argc >= 3) CHECK(stations.load(argv[2]));

    fh6::AudioSourceManager mgr{1 << 20};
    auto& hook = RadioHook::instance();
    if (with_platform) {
        CHECK(wait_for_platform(10000) != nullptr && platform_v3() != nullptr);
        bool ok = false; // the platform says when the game is ready to hook
        for (int i = 0; i < 200 && !ok; ++i)
            if (!(ok = hook.install(img, mgr, settings, stations))) Sleep(50);
        CHECK(ok);
        CHECK(hook.via_platform());
    } else {
        CHECK(hook.install(img, mgr, settings, stations));
        CHECK(!hook.via_platform());
    }

    std::byte* create_site = img.at(rva::kTrackCreateSoundCall);
    std::byte* sub_site    = img.at(rva::kTrackGetSubSoundCall);
    auto* create_stub      = const_cast<std::byte*>(call_target(create_site));
    auto* sub_stub         = const_cast<std::byte*>(call_target(sub_site));
    CHECK(create_stub != img.at(rva::kCreateSound));
    CHECK(sub_stub != img.at(rva::kGetSubSound));
    // Bytes after the patched call are untouched.
    CHECK(static_cast<uint8_t>(create_site[5]) == 0xB9); // mov ecx,28h
    CHECK(static_cast<uint8_t>(sub_site[5]) == 0x8B);    // mov edx,[rbx+28h]

    // Swap FMOD for fakes inside the mapped image (after install verified it).
    write_jmp(img.at(rva::kCreateSound), reinterpret_cast<void*>(&fake_create_sound));
    write_jmp(img.at(rva::kGetSubSound), reinterpret_cast<void*>(&fake_get_sub_sound));
    write_jmp(img.at(rva::kSoundGetUserData), reinterpret_cast<void*>(&fake_get_user_data));
    write_jmp(img.at(rva::kOperatorNew), reinterpret_cast<void*>(&fake_new));

    void* system = reinterpret_cast<void*>(0x5157E3);
    std::byte bank_exinfo[fmod::kExinfoSize]{};
    void* out = nullptr;

    // 1) Track on the takeover station (7) -> user stream.
    hook.push_metadata("Bohemian Rhapsody - Remastered 2011 With A Really Long Title", "Queen");
    auto* il = make_sample(img, "HZ2_RIL_Classixx_AStrangerLove", "/Master/Radio/Track",
                           "Stranger Love (RAC Remix)", "Classixx", 315334);
    const char* bank7 = "Game:\\Media\\Audio\\FmodAnthem\\Durango\\HZ2_MX_Radio_07_Tracks.bank";
    uint32_t r = call_like_game(create_stub, il->rs, system, bank7, 0x00000088u, bank_exinfo, &out);
    CHECK(r == 0);
    CHECK(g_calls.size() == 1);
    auto& c1 = g_calls.back();
    CHECK(c1.system == system);
    CHECK(c1.name_null);
    CHECK((c1.mode & fmod::OPENUSER) && (c1.mode & fmod::CREATESTREAM) && (c1.mode & fmod::LOOP_NORMAL));
    CHECK((c1.mode & fmod::MODE_2D) != 0);
    CHECK(c1.sound->freq == 48000 && c1.sound->channels == 2 && c1.sound->format == 2);
    CHECK(c1.sound->length == 0xFFFFFFFFu);
    CHECK(c1.sound->pcmread != nullptr && c1.sound->userdata != nullptr);
    CHECK(out == c1.sound);
    CHECK(*reinterpret_cast<int32_t*>(il->props + layout::kPropSampleLength) == 16 * 60 * 1000);
    CHECK(il->markers[7] == 16 * 60 * 1000 && il->markers[1] == 227);
    CHECK(*reinterpret_cast<int32_t*>(il->rs + layout::kStreamStartMs) == 123456);
    CHECK(get_str(il->props + layout::kPropDisplayName) ==
          "Bohemian Rhapsody - Remastered 2011 With A Really Long Title");
    CHECK(get_str(il->props + layout::kPropArtist) == "Queen");

    // getSubSound through the patched site: ours -> itself, vanilla -> FMOD's answer.
    void* sub = nullptr;
    auto sub_fn = reinterpret_cast<uint32_t (*)(void*, int, void**)>(sub_stub);
    CHECK(sub_fn(c1.sound, 3, &sub) == 0 && sub == c1.sound);
    FakeSound bank_sound;
    CHECK(sub_fn(&bank_sound, 3, &sub) == 0 && sub == &g_bank_sub);

    // PCM: ring -> stream, gain applied.
    std::vector<int16_t> pcm(4096);
    for (std::size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<int16_t>(i * 7 - 9000);
    mgr.ring().write(pcm.data(), pcm.size() * 2);
    std::vector<int16_t> outbuf(4096, 0x1234);
    auto read = reinterpret_cast<PcmRead>(c1.sound->pcmread);
    hook.set_gain(1.0f);
    CHECK(read(c1.sound, outbuf.data(), 2048 * 2) == 0);
    CHECK(std::memcmp(outbuf.data(), pcm.data(), 2048 * 2) == 0);
    hook.set_gain(0.5f);
    CHECK(read(c1.sound, outbuf.data(), 2048 * 2) == 0);
    CHECK(outbuf[0] == static_cast<int16_t>(std::lrintf(pcm[2048] * 0.5f)));
    hook.set_gain(1.0f);
    // Underrun -> silence, counted.
    const auto under0 = hook.stats().underruns.load();
    CHECK(read(c1.sound, outbuf.data(), 512) == 0);
    CHECK(outbuf[0] == 0 && hook.stats().underruns.load() == under0 + 1);

    // Real-time discard (game not listening) eats from the ring.
    {
        std::vector<int16_t> fill(4096, 7);
        mgr.ring().drain();
        mgr.ring().write(fill.data(), 8192);
        hook.discard_realtime(4096 + 2); // rounded down to whole frames
        CHECK(mgr.ring().readable() == 4096);
        mgr.ring().drain();
    }

    // Live metadata update (in place, no allocation from this thread).
    hook.push_metadata("Don't Stop Me Now", "Queen");
    CHECK(get_str(il->props + layout::kPropDisplayName) == "Don't Stop Me Now");

    // A freed / reused owner is never written to from the control thread.
    {
        void* bogus = nullptr;
        void* real;
        std::memcpy(&real, il->rs, 8);
        std::memcpy(il->rs, &bogus, 8);
        hook.push_metadata("Should Not Land", "Nobody");
        CHECK(get_str(il->props + layout::kPropDisplayName) == "Don't Stop Me Now");
        std::memcpy(il->rs, &real, 8);
    }

    // 2) DJ on the takeover station -> short silent stream.
    auto* dj = make_sample(img, "HZ2_VO_RadioDJ7_BarnFinds_BarnFind10Seen_1", "/Master/Radio/DJ",
                           "", "", 13843);
    const char* djbank = "Game:\\Media\\Audio\\FmodAnthem\\Durango\\HZ2_MX_Radio_07_DJ_EN.bank";
    r = call_like_game(create_stub, dj->rs, system, djbank, 0x88u, bank_exinfo, &out);
    CHECK(r == 0 && g_calls.size() == 2);
    CHECK(g_calls.back().name_null && (g_calls.back().mode & fmod::LOOP_OFF));
    CHECK(g_calls.back().sound->length == 48000 * 4 / 4);
    std::vector<int16_t> dj_out(512, 0x77);
    mgr.ring().write(pcm.data(), 1024);
    reinterpret_cast<PcmRead>(g_calls.back().sound->pcmread)(g_calls.back().sound, dj_out.data(), 1024);
    CHECK(dj_out[0] == 0 && dj_out[100] == 0);
    CHECK(mgr.ring().readable() == 1024); // the DJ stream didn't eat the music
    CHECK(sub_fn(g_calls.back().sound, 0, &sub) == 0 && sub == g_calls.back().sound);

    // An older track stream stops being fed once a newer one exists.
    auto* il2 = make_sample(img, "HZ2_RIL_BassDrumOfDeath_Electric", "/Master/Radio/Track",
                            "Electric", "Bass Drum Of Death", 199340);
    r = call_like_game(create_stub, il2->rs, system, bank7, 0x88u, bank_exinfo, &out);
    CHECK(r == 0 && g_calls.back().name_null);
    std::vector<int16_t> o2(256, 0x55);
    read(c1.sound, o2.data(), 512);
    CHECK(o2[0] == 0 && mgr.ring().readable() == 1024);
    reinterpret_cast<PcmRead>(g_calls.back().sound->pcmread)(g_calls.back().sound, o2.data(), 512);
    CHECK(std::memcmp(o2.data(), pcm.data(), 512) == 0);

    // 3) Track on another station -> vanilla, args untouched.
    auto* hp = make_sample(img, "HZ2_R1_Chromeo_ComeAlive", "/Master/Radio/Track",
                           "Come Alive (feat. Toro Y Moi)", "Chromeo", 236936);
    const char* bank1 = "Game:\\Media\\Audio\\FmodAnthem\\Durango\\HZ2_MX_Radio_01_Tracks.bank";
    r = call_like_game(create_stub, hp->rs, system, bank1, 0x10088u, bank_exinfo, &out);
    CHECK(r == 0 && !g_calls.back().name_null && g_calls.back().name == bank1);
    CHECK(g_calls.back().mode == 0x10088u);
    CHECK(*reinterpret_cast<int32_t*>(hp->props + layout::kPropSampleLength) == 236936);
    CHECK(get_str(hp->props + layout::kPropDisplayName) == "Come Alive (feat. Toro Y Moi)");

    // 4) Takeover disabled -> the station-7 entry goes back to its original state.
    {
        std::ofstream f{ini, std::ios::app};
        f << "\n[takeover]\nenabled=0\n";
    }
    Sleep(1100);
    {
        // Rewrite the whole file so GetPrivateProfileInt sees a single section.
        std::ofstream f{ini, std::ios::trunc};
        f << "[takeover]\nenabled=0\nstation=7\n";
    }
    CHECK(settings.refresh());
    CHECK(!settings.get().enabled);
    r = call_like_game(create_stub, il->rs, system, bank7, 0x88u, bank_exinfo, &out);
    CHECK(r == 0 && !g_calls.back().name_null);
    CHECK(*reinterpret_cast<int32_t*>(il->props + layout::kPropSampleLength) == 315334);
    CHECK(il->markers[7] == 315334 - 4);
    CHECK(get_str(il->props + layout::kPropDisplayName) == "Stranger Love (RAC Remix)");
    CHECK(get_str(il->props + layout::kPropArtist) == "Classixx");

    // 5) Not a RadioStreamFmod in rdi -> pure passthrough.
    std::byte junk[0x60]{};
    r = call_like_game(create_stub, junk, system, bank7, 0x88u, bank_exinfo, &out);
    CHECK(r == 0 && !g_calls.back().name_null);

    // 5b) Sample details unreadable -> classify from the bank name alone.
    {
        auto* bare = make_sample(img, "x", "/Master/Radio/Track", "a", "b", 1000);
        std::byte* nothing = nullptr;
        std::memcpy(bare->rs + layout::kStreamSample, &nothing, 8);
        std::ofstream f{ini, std::ios::trunc};
        f << "[takeover]\nenabled=1\nstation=7\n";
    }
    Sleep(1100);
    {
        std::ofstream f{ini, std::ios::trunc};
        f << "[takeover]\nenabled=1\nstation=7\n";
    }
    settings.refresh();
    {
        auto* bare = make_sample(img, "x", "/Master/Radio/Track", "a", "b", 1000);
        std::byte* nothing = nullptr;
        std::memcpy(bare->rs + layout::kStreamSample, &nothing, 8);
        r = call_like_game(create_stub, bare->rs, system, bank7, 0x88u, bank_exinfo, &out);
        CHECK(r == 0 && g_calls.back().name_null && (g_calls.back().mode & fmod::LOOP_NORMAL));
        r = call_like_game(create_stub, bare->rs, system, djbank, 0x88u, bank_exinfo, &out);
        CHECK(r == 0 && g_calls.back().name_null && (g_calls.back().mode & fmod::LOOP_OFF));
    }

    // 6) station map fallback when the name is a memory blob.
    {
        std::ofstream f{ini, std::ios::trunc};
        f << "[takeover]\nenabled=1\nstation=7\n";
    }
    Sleep(1100);
    {
        std::ofstream f{ini, std::ios::trunc};
        f << "[takeover]\nenabled=1\nstation=7\n";
    }
    settings.refresh();
    if (argc >= 3) {
        r = call_like_game(create_stub, il->rs, system, reinterpret_cast<const char*>(bank_exinfo),
                           0x88u | fmod::OPENMEMORY, bank_exinfo, &out);
        CHECK(r == 0 && g_calls.back().name_null);
    }

    hook.verify();
    CHECK(call_target(create_site) == create_stub);

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    std::printf("log: %s\\test.log\n", dir.c_str());
    return g_fail ? 1 : 0;
}
