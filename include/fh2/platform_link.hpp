#pragma once

// What the FH2 Mod Platform handed us in fh2mod_init (src/proxy/dll_main.cpp).

#include "fh2mod.h"

#include <cstdint>

namespace fh2 {

// Called from fh2mod_init.
void set_platform(const FH2ModApi* api, FH2Mod* mod) noexcept;
// Waits (up to `ms`) for fh2mod_init; null if there's no platform. Only the
// first call waits.
const FH2ModApi* wait_for_platform(uint32_t ms) noexcept;
// The platform, if it's new enough for hooks (API 3, platform 2.2), else null.
const FH2ModApi* platform_v3() noexcept;
FH2Mod* platform_mod() noexcept;

} // namespace fh2
