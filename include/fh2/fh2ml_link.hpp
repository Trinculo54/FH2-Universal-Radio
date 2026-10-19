#pragma once

// What FH2ML handed us in Initialize (src/proxy/dll_main.cpp).

#include "FH2ML_v4.h"

namespace fh2 {

// Called from Initialize, before any of the radio's threads start.
void set_services(const FH2ML_Services_v4* services, FH2ML_Mod* mod) noexcept;
// FH2ML's services, or null if there are none (only the offline hook test
// runs without them; the radio then patches the game itself).
const FH2ML_Services_v4* services() noexcept;
FH2ML_Mod* mod_handle() noexcept;

} // namespace fh2
