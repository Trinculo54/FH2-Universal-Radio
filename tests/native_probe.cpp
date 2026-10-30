// native_probe.exe -- on-device check for the Wine/Proton native helper path.
// Run it inside the game's Proton prefix, e.g.
//   protontricks-launch --appid <id> native_probe.exe <helper> <yt-dlp> <ffmpeg>
// It prints, step by step:
//   1. whether ntdll exports wine_get_version (Wine detection)
//   2. what happens when CreateProcess targets an ELF with a pipe as stdout
//      (expected: TRUE, NULL hProcess, 0 bytes read -> stdio not connected)
//   3. helper launch + hello, then `ffmpeg -version` and `yt-dlp --version`
//      captured over the loopback socket, with wall-clock timings.
#include "fh6/native_bridge.hpp"
#include "fh6/subprocess.hpp"

#include <chrono>
#include <cstdio>
#include <string>

using namespace fh6;

static double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: native_probe.exe <helper> <yt-dlp> <ffmpeg>   (Unix paths)\n");
        return 2;
    }
    const bool wine = native::running_under_wine();
    std::printf("[1] wine_get_version: %s\n", wine ? native::wine_version().c_str() : "(not Wine)");
    if (!wine) return 1;

    // [2] Direct ELF spawn with an anonymous pipe on stdout.
    {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE rd = nullptr, wr = nullptr;
        CreatePipe(&rd, &wr, &sa, 1 << 16);
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        std::wstring dos = L"Z:" + subprocess::widen(argv[3]);
        for (auto& c : dos) if (c == L'/') c = L'\\';
        std::wstring cmd = subprocess::quote(dos) + L" -version";
        STARTUPINFOW si{};
        si.cb         = sizeof(si);
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdInput  = subprocess::open_nul(GENERIC_READ);
        si.hStdOutput = wr;
        si.hStdError  = wr;
        PROCESS_INFORMATION pi{};
        BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr,
                                 &si, &pi);
        DWORD ec = GetLastError();
        CloseHandle(wr);
        Sleep(1500);
        DWORD avail = 0;
        PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr);
        std::printf("[2] CreateProcess(ELF ffmpeg, pipe stdout): ok=%d err=%lu hProcess=%p "
                    "pid=%lu bytes_in_pipe=%lu\n",
                    ok, ok ? 0ul : ec, (void*)pi.hProcess, pi.dwProcessId, avail);
        CloseHandle(rd);
    }

    // [3] Helper path.
    NativeConfig cfg{true, argv[1], argv[2], argv[3]};
    native::NativeHelper h;
    auto t0       = std::chrono::steady_clock::now();
    const bool up = h.start(cfg);
    std::printf("[3] helper start: %s (%.0f ms)\n", up ? "OK" : "FAILED (see helpers.log)",
                ms_since(t0));
    if (!up) return 1;
    t0       = std::chrono::steady_clock::now();
    auto ffv = h.run_capture({"ffmpeg", {"-hide_banner", "-version"}}, 10000);
    std::printf("    ffmpeg -version: %zu bytes in %.0f ms: %.60s\n", ffv.size(), ms_since(t0),
                ffv.c_str());
    t0       = std::chrono::steady_clock::now();
    auto ytv = h.run_capture({"yt-dlp", {"--version"}}, 30000);
    std::printf("    yt-dlp --version: %zu bytes in %.0f ms: %s\n", ytv.size(), ms_since(t0),
                ytv.c_str());
    h.stop();
    return (ffv.empty() || ytv.empty()) ? 1 : 0;
}
