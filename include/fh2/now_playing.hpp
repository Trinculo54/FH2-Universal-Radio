#pragma once
// What the FH2UniversalRadio.NowPlaying interface hands out to other mods
// (include/fh2universalradio.h), kept up to date by the control loop.

#include "fh2universalradio.h"

#include <string>

namespace fh2 {

void set_now_playing(bool playing, const std::string& source, const std::string& title, const std::string& artist);
const FH2RadioNowPlayingV1* now_playing_interface() noexcept;

} // namespace fh2
