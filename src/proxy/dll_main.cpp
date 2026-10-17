// FH2 Universal Radio plugin entry point (fh2-mods\FH2UniversalRadio.dll).
//
// Loaded by the FH2 Mod Loader (loader/, d3dcompiler_47.dll next to
// azimuth.exe). DllMain only spawns the bridge thread; everything else
// (waiting for the game image, patching the radio, HTTP, Spotify) happens
// off the loader lock.

#include <windows.h>

namespace fh6 {
void run_bridge(HMODULE self) noexcept;
}

namespace {
DWORD WINAPI bridge_thread(LPVOID param) {
    fh6::run_bridge(static_cast<HMODULE>(param));
    return 0;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        // Pin ourselves: the bridge thread and the patched game code call
        // into this module for the life of the process.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&bridge_thread), &pinned);
        if (HANDLE t = CreateThread(nullptr, 0, bridge_thread, instance, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
