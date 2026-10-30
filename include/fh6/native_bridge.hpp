#pragma once

// Wine/Proton only: run yt-dlp / ffmpeg as NATIVE Linux processes.
//
// Under Wine, CreateProcess on an ELF does exec it (ntdll fork_and_exec), but
// the child only gets real stdio when the handles map to Unix fds. Wine pipes
// are wineserver pseudo-fds, so the child's stdin/stdout silently become
// /dev/null, and no process handle is returned. We therefore launch one small
// native helper (native/fh2_native_helper.c) at startup -- its stdio doesn't
// matter -- and ask it over loopback TCP to run pipelines whose last stage
// writes straight into the socket. Closing the socket kills the pipeline; the
// helper exits when its keepalive connection (held here) drops.

#include "fh6/config.hpp"

#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace fh6::native {

// True when ntdll exports wine_get_version (Wine, Proton, CrossOver).
bool running_under_wine() noexcept;
std::string wine_version();

// Windows path -> Unix path (kernel32!wine_get_unix_file_name). "" off Wine.
std::string to_unix_path(const std::filesystem::path& p);

// Config value ('/home/deck/x' or 'Z:\home\deck\x') -> Unix path.
std::string normalize_unix(const std::string& p);

struct Stage {
    const char* tool; // "yt-dlp" | "ffmpeg" (helper-side allowlist)
    std::vector<std::string> args;
};

class NativeHelper {
public:
    NativeHelper() = default;
    ~NativeHelper();
    NativeHelper(const NativeHelper&)            = delete;
    NativeHelper& operator=(const NativeHelper&) = delete;

    // Launch the helper and connect. False (and logs why) when native mode is
    // disabled, we're not under Wine, a path is missing, or it never answers.
    bool start(const NativeConfig& cfg);
    void stop() noexcept;
    bool alive() const noexcept;

    // Start a pipeline; returns a non-blocking socket carrying the last
    // stage's stdout until EOF, or INVALID_SOCKET. closesocket() kills it.
    SOCKET open_run(const std::vector<Stage>& chain);

    // Run one command and return its stdout (blocking, bounded by timeout).
    std::string run_capture(const Stage& stage, DWORD timeout_ms = 120000);

private:
    SOCKET connect_send(const char* op, const std::vector<Stage>& chain, DWORD timeout_ms);

    mutable std::mutex mu_;
    SOCKET keepalive_ = INVALID_SOCKET;
    std::string token_;
    std::uint16_t port_ = 0;
    bool wsa_           = false;
};

// Non-blocking read helper: >0 = bytes read, 0 = nothing available yet,
// -1 = EOF or error (the pipeline has finished or died).
int sock_read(SOCKET s, void* buf, int len) noexcept;

} // namespace fh6::native
