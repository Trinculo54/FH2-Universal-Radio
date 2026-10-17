#pragma once

// Everything FH2-specific that was recovered from the Xbox One build of
// Forza Horizon 2 (forza_durango_release_final.exe, link timestamp
// 0x565501ED, SizeOfImage 0x3A18000) as mapped by Azimuth / Mezcal.
//
// The game links FMOD Studio 1.x statically and *exports* the whole FMOD
// low-level C++ API by its mangled name, so the FMOD entry points are
// verified against the export table at runtime instead of being trusted
// blindly. The two call sites and the game-object layouts are fixed for
// this single build and are byte-verified before anything is patched.

#include <cstdint>

namespace fh2 {

namespace image {
constexpr uint32_t kTimestamp   = 0x565501EDu;
constexpr uint32_t kSizeOfImage = 0x03A18000u;
} // namespace image

namespace rva {
// FMOD exports (RVA + mangled name, both checked).
constexpr uint32_t kCreateSound      = 0x01997740u;
constexpr uint32_t kGetSubSound      = 0x0199F510u;
constexpr uint32_t kSoundGetUserData = 0x0199FBE0u;

// RadioStreamFmod::load(sample, ...) -> FMOD::System::createSound(
//     soundinfo.name_or_data, soundinfo.mode | NONBLOCKING | CREATESTREAM,
//     &soundinfo.exinfo, &this->sound)
// `rdi` holds the RadioStreamFmod* at this call.
constexpr uint32_t kTrackCreateSoundCall = 0x01018A13u;
// RadioStreamFmod::tryStart() -> FMOD::Sound::getSubSound(
//     userdata->subsound_index, &userdata->subsound)
constexpr uint32_t kTrackGetSubSoundCall = 0x010221F2u;

// RTTI-confirmed vtable of RadioStreamFmod (".?AVRadioStreamFmod@@").
constexpr uint32_t kRadioStreamFmodVtbl = 0x02E76220u;
// RTTI-confirmed sample vtables. The radio only ever loads the subclasses;
// all four share IRadioSample's props getter (sample+0x08).
constexpr uint32_t kRadioSampleVtbl = 0x02E74ED8u; // ".?AVRadioSample@@" (base)
constexpr uint32_t kMusicSampleVtbl = 0x02E74EF8u; // ".?AVMusicSample@@"  tracks
constexpr uint32_t kDJSampleVtbl    = 0x02E76570u; // ".?AVDJSample@@"     DJ lines
constexpr uint32_t kIdentSampleVtbl = 0x02E76590u; // ".?AVIdentSample@@"  station jingles

// The game's global operator new(size_t). Only ever called from a game thread.
constexpr uint32_t kOperatorNew = 0x003229C0u;
} // namespace rva

namespace exports {
constexpr const char* kCreateSound =
    "?createSound@System@FMOD@@QEAA?AW4FMOD_RESULT@@PEBDIPEAUFMOD_CREATESOUNDEXINFO@@PEAPEAVSound@2@@Z";
constexpr const char* kGetSubSound =
    "?getSubSound@Sound@FMOD@@QEAA?AW4FMOD_RESULT@@HPEAPEAV12@@Z";
constexpr const char* kSoundGetUserData =
    "?getUserData@Sound@FMOD@@QEAA?AW4FMOD_RESULT@@PEAPEAX@Z";
} // namespace exports

// Object layouts (x64 MSVC 2012 STL: std::string = 32 bytes, SSO 16).
namespace layout {
// RadioStreamFmod
constexpr std::ptrdiff_t kStreamSound  = 0x08; // FMOD::Sound*
constexpr std::ptrdiff_t kStreamEvent  = 0x10; // FMOD::Studio::EventInstance*
constexpr std::ptrdiff_t kStreamStartMs = 0x28; // int32 timeline seek applied by tryStart
constexpr std::ptrdiff_t kStreamSample = 0x30; // std::shared_ptr<IRadioSample> (raw ptr)

// RadioSample
constexpr std::ptrdiff_t kSampleProps = 0x08; // SampleProperties*

// IRadioSample::SampleProperties (filled from media\audio\RadioInfo_XX.xml)
constexpr std::ptrdiff_t kPropSoundName    = 0x00; // std::string
constexpr std::ptrdiff_t kPropEventName    = 0x20; // std::string  "/Master/Radio/Track"
constexpr std::ptrdiff_t kPropDisplayName  = 0x40; // std::string  shown in the radio HUD
constexpr std::ptrdiff_t kPropArtist       = 0x60; // std::string
constexpr std::ptrdiff_t kPropSampleLength = 0x8C; // int32 ms; RadioStation ends the track here
constexpr std::ptrdiff_t kPropMarkers      = 0x90; // std::vector<int32> (begin, end, cap)

// markers[]: 0 = 0, 1 = TrackStart, 2 = DJDrop, 3 = TrackDrop,
//            4 = TrackBreakDown, 5 = StingerStart, 6 = DJStart, 7 = End
constexpr int kMarkerStingerStart = 5;
constexpr int kMarkerDJStart      = 6;
constexpr int kMarkerEnd          = 7;
constexpr int kMarkerCount        = 8;
} // namespace layout

// FMOD 1.x constants used here.
namespace fmod {
constexpr uint32_t kOk = 0;

constexpr uint32_t LOOP_OFF         = 0x00000001;
constexpr uint32_t LOOP_NORMAL      = 0x00000002;
constexpr uint32_t MODE_2D          = 0x00000008;
constexpr uint32_t MODE_3D          = 0x00000010;
constexpr uint32_t CREATESTREAM     = 0x00000080;
constexpr uint32_t OPENUSER         = 0x00000400;
constexpr uint32_t OPENMEMORY       = 0x00000800;
constexpr uint32_t NONBLOCKING      = 0x00010000;
constexpr uint32_t OPENMEMORY_POINT = 0x10000000;

constexpr int32_t FORMAT_PCM16 = 2;

// FMOD_CREATESOUNDEXINFO as compiled into this build (cbsize 0xD8, same
// value the game itself passes for its Xbox Music user stream).
constexpr uint32_t kExinfoSize          = 0xD8;
constexpr std::ptrdiff_t kExCbSize      = 0x00;
constexpr std::ptrdiff_t kExLength      = 0x04;
constexpr std::ptrdiff_t kExNumChannels = 0x0C;
constexpr std::ptrdiff_t kExFrequency   = 0x10;
constexpr std::ptrdiff_t kExFormat      = 0x14;
constexpr std::ptrdiff_t kExDecodeBuf   = 0x18;
constexpr std::ptrdiff_t kExPcmRead     = 0x38;
constexpr std::ptrdiff_t kExPcmSetPos   = 0x40;
constexpr std::ptrdiff_t kExUserData    = 0x68;
} // namespace fmod

} // namespace fh2
