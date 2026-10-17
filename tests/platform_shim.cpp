// For fh2_hook_test with FH2_TEST_PLATFORM=1: a mod the real FH2 Mod Platform
// loads from fh2-mods, which hands the platform's API to the test (running
// as azimuth.exe), so the radio's hooks go through the real platform.
#include "fh2mod.h"

#include <windows.h>

using SetPlatformFn = void (*)(const FH2ModApi*, FH2Mod*);

FH2MOD_EXPORT int fh2mod_init(const FH2ModApi* api) {
    FH2ModInfo info{sizeof info, "FH2 Universal Radio", "test", nullptr, FH2MOD_PLATFORM_VERSION, 0};
    FH2Mod* mod = api->register_mod(&info);
    auto set    = reinterpret_cast<SetPlatformFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(nullptr), "fh2_test_set_platform")));
    if (!mod || !set) return 1;
    set(api, mod);
    return 0;
}
