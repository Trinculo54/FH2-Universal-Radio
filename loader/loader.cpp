// FH2 Mod Loader -- d3dcompiler_47.dll proxy for Azimuth.
//
// azimuth.exe statically imports d3dcompiler_47.dll (for D3DCompile) and the
// loader looks next to the exe first, so this DLL gets loaded at startup
// without touching any of Azimuth's own files, and without fighting Forza
// Horizon 2 Extra Options (which owns winmm.dll). Every export is a PE
// forwarder to the real System32 DLL (d3dcompiler_47.def for llvm-mingw,
// msvc_exports.inc for MSVC), so Azimuth's shader compiles never touch it.
//
// Its only job: load every *.dll in the fh2-mods folder next to azimuth.exe.
// Each mod (FH2 Universal Radio, WheresMyBuddy, ...) is one DLL in there and
// starts its own work from DllMain. Every mod ships this same file, so
// installing any combination of them just works.
//
// Loading happens on a thread so it runs outside the loader lock; that's
// still within a few milliseconds of Azimuth starting, long before it maps
// the game. Results go to fh2-mods\loader.log.

#include <windows.h>

#ifdef _MSC_VER
#include "msvc_exports.inc"
#endif

namespace {

wchar_t g_dir[MAX_PATH];

void log_line(HANDLE log, const wchar_t* fmt, const wchar_t* a = L"", DWORD b = 0) {
    if (log == INVALID_HANDLE_VALUE) return;
    wchar_t w[1024];
    wsprintfW(w, fmt, a, b);
    char u[3072];
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof(u), nullptr, nullptr);
    DWORD written = 0;
    if (n > 1) WriteFile(log, u, static_cast<DWORD>(n - 1), &written, nullptr);
}

DWORD WINAPI load_mods(LPVOID) {
    wchar_t pattern[MAX_PATH], path[MAX_PATH], logpath[MAX_PATH];
    lstrcpyW(pattern, g_dir);
    lstrcatW(pattern, L"fh2-mods\\*.dll");
    lstrcpyW(logpath, g_dir);
    lstrcatW(logpath, L"fh2-mods\\loader.log");

    HANDLE log = CreateFileW(logpath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    log_line(log, L"FH2 Mod Loader: loading %s\r\n", pattern);

    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    int loaded  = 0;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            lstrcpyW(path, g_dir);
            lstrcatW(path, L"fh2-mods\\");
            lstrcatW(path, fd.cFileName);
            // ALTERED_SEARCH_PATH: a mod's own dependencies resolve from fh2-mods first.
            if (LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) {
                ++loaded;
                log_line(log, L"  loaded %s\r\n", fd.cFileName);
            } else {
                log_line(log, L"  FAILED %s (error %lu)\r\n", fd.cFileName, GetLastError());
            }
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    } else {
        log_line(log, L"  no fh2-mods folder or no mods in it\r\n");
    }
    log_line(log, L"%s%lu mod(s) loaded\r\n", L"", static_cast<DWORD>(loaded));
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        const DWORD n = GetModuleFileNameW(instance, g_dir, MAX_PATH);
        if (!n || n >= MAX_PATH) return TRUE;
        for (DWORD i = n; i > 0; --i) {
            if (g_dir[i - 1] == L'\\' || g_dir[i - 1] == L'/') {
                g_dir[i] = 0;
                break;
            }
        }
        // Only act inside Azimuth (or Mezcal's game exe), never in other
        // programs that happen to load d3dcompiler_47.dll from this folder.
        wchar_t exe[MAX_PATH];
        const DWORD m = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const wchar_t* base = exe;
        for (DWORD i = 0; i < m; ++i)
            if (exe[i] == L'\\' || exe[i] == L'/') base = exe + i + 1;
        if (lstrcmpiW(base, L"azimuth.exe") && lstrcmpiW(base, L"forza_durango_release_final.exe"))
            return TRUE;
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&load_mods), &pinned);
        if (HANDLE t = CreateThread(nullptr, 0, load_mods, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
