// d3dcompiler_47.dll proxy for Azimuth.
//
// azimuth.exe statically imports d3dcompiler_47.dll (for D3DCompile) and the
// loader looks next to the exe first, so dropping this DLL beside
// azimuth.exe gets us loaded at startup without touching any of Azimuth's
// own files -- and without fighting Forza Horizon 2 Extra Options, which
// already owns winmm.dll. Every export is a PE forwarder to the real system
// DLL (src/proxy/d3dcompiler_47.def for llvm-mingw, the pragmas below for
// MSVC), so Azimuth's shader compiles never run through our code.
//
// DllMain only spawns the bridge thread; everything else (waiting for the
// game image, patching the radio, HTTP, Spotify) happens off the loader lock.

#include <windows.h>

#ifdef _MSC_VER
#include "msvc_exports.inc"
#endif

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
