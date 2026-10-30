#include "fh6/native_bridge.hpp"
#include "fh6/log.hpp"
#include "fh6/subprocess.hpp"

#include <ws2tcpip.h>
#include <bcrypt.h>


namespace fh6::native {

namespace {

using wine_get_version_fn        = const char*(__cdecl*)();
using wine_get_unix_file_name_fn = char*(__cdecl*)(const wchar_t*);
using wine_get_dos_file_name_fn  = wchar_t*(__cdecl*)(const char*);

template <class Fn> Fn wine_export(const wchar_t* module, const char* name) {
    HMODULE m = GetModuleHandleW(module);
    return m ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, name))) : nullptr;
}

// Unix path -> Wine DOS path (Z:\...), for CreateProcess / file checks.
std::wstring to_dos_path(const std::string& unix_path) {
    auto fn = wine_export<wine_get_dos_file_name_fn>(L"kernel32.dll", "wine_get_dos_file_name");
    if (!fn || unix_path.empty()) return {};
    wchar_t* w = fn(unix_path.c_str());
    if (!w) return {};
    std::wstring out{w};
    HeapFree(GetProcessHeap(), 0, w);
    return out;
}

std::string random_hex(std::size_t bytes) {
    std::vector<unsigned char> raw(bytes);
    if (BCryptGenRandom(nullptr, raw.data(), (ULONG)raw.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return {};
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char b : raw) {
        out += hex[b >> 4];
        out += hex[b & 0x0F];
    }
    return out;
}

void append_field(std::string& out, const std::string& s) {
    out += std::to_string(s.size());
    out += '\n';
    out += s;
}

bool send_all(SOCKET s, const std::string& data) {
    std::size_t off = 0;
    while (off < data.size()) {
        int n = send(s, data.data() + off, (int)(data.size() - off), 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        off += (std::size_t)n;
    }
    return true;
}

void set_nonblocking(SOCKET s, bool on) {
    u_long mode = on ? 1 : 0;
    ioctlsocket(s, FIONBIO, &mode);
}

void set_recv_timeout(SOCKET s, DWORD ms) {
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
}

} // namespace

bool running_under_wine() noexcept {
    return wine_export<wine_get_version_fn>(L"ntdll.dll", "wine_get_version") != nullptr;
}

std::string wine_version() {
    auto fn = wine_export<wine_get_version_fn>(L"ntdll.dll", "wine_get_version");
    return fn ? std::string{fn()} : std::string{};
}

std::string to_unix_path(const std::filesystem::path& p) {
    auto fn = wine_export<wine_get_unix_file_name_fn>(L"kernel32.dll", "wine_get_unix_file_name");
    if (!fn || p.empty()) return {};
    char* u = fn(p.wstring().c_str());
    if (!u) return {};
    std::string out{u};
    HeapFree(GetProcessHeap(), 0, u);
    return out;
}

std::string normalize_unix(const std::string& p) {
    if (p.empty() || p.front() == '/') return p;
    return to_unix_path(std::filesystem::path{subprocess::widen(p)});
}

NativeHelper::~NativeHelper() { stop(); }

bool NativeHelper::start(const NativeConfig& cfg) {
    std::scoped_lock lk{mu_};
    if (keepalive_ != INVALID_SOCKET) return true;
    if (!cfg.enabled) return false;
    if (!running_under_wine()) {
        log::info("[native] [native].enabled is set but this is not Wine/Proton -- ignoring");
        return false;
    }

    const std::string helper = normalize_unix(cfg.helper);
    const std::string ytdlp  = normalize_unix(cfg.yt_dlp);
    const std::string ffmpeg = normalize_unix(cfg.ffmpeg);
    for (const auto& [key, path] : {std::pair{"helper", helper}, std::pair{"yt_dlp", ytdlp},
                                    std::pair{"ffmpeg", ffmpeg}}) {
        const std::wstring dos = to_dos_path(path);
        if (path.empty() || dos.empty() ||
            GetFileAttributesW(dos.c_str()) == INVALID_FILE_ATTRIBUTES) {
            log::warn("[native] [native].{} = '{}' not found -- using the Windows .exe path", key,
                      path);
            return false;
        }
    }

    WSADATA wsa{};
    if (!wsa_) {
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            log::warn("[native] WSAStartup failed");
            return false;
        }
        wsa_ = true;
    }

    token_ = random_hex(16);
    if (token_.empty()) return false;
    const std::string log_unix = to_unix_path(subprocess::stderr_log_path());
    const std::wstring helper_dos = to_dos_path(helper);

    // A port collision just makes the helper exit; retry on a different one.
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::string r = random_hex(2);
        port_ = static_cast<std::uint16_t>(30000 + (std::stoul(r, nullptr, 16) % 30000));

        std::wstring cmd = subprocess::quote(helper_dos) + L" --port " + std::to_wstring(port_) +
                           L" --token " + subprocess::widen(token_) + L" --yt-dlp " +
                           subprocess::quote(subprocess::widen(ytdlp)) + L" --ffmpeg " +
                           subprocess::quote(subprocess::widen(ffmpeg));
        if (!log_unix.empty())
            cmd += L" --log " + subprocess::quote(subprocess::widen(log_unix));

        // Wine fork()+exec()s the ELF directly (ntdll fork_and_exec): TRUE is
        // returned with NULL process/thread handles, and CREATE_NO_WINDOW puts
        // it in its own session with stdio on /dev/null. One-time cost, at
        // startup, same as launching fh2-radio-worker.exe.
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(helper_dos.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, subprocess::safe_spawn_cwd(), &si, &pi)) {
            log::warn("[native] failed to launch helper {} (err {})", helper, GetLastError());
            return false;
        }
        if (pi.hThread) CloseHandle(pi.hThread);
        if (pi.hProcess) CloseHandle(pi.hProcess);

        const ULONGLONG deadline = GetTickCount64() + 3000;
        while (GetTickCount64() < deadline) {
            SOCKET s = connect_send("hello", {}, 1000);
            if (s != INVALID_SOCKET) {
                char ok[4]{};
                int got = recv(s, ok, 3, 0);
                if (got == 3 && ok[0] == 'O' && ok[1] == 'K') {
                    set_nonblocking(s, true);
                    keepalive_ = s;
                    log::info("[native] helper up on 127.0.0.1:{} ({}) -- YouTube uses native "
                              "yt-dlp={} ffmpeg={}",
                              port_, wine_version(), ytdlp, ffmpeg);
                    return true;
                }
                closesocket(s);
            }
            Sleep(50);
        }
        log::warn("[native] helper did not answer on port {} (attempt {})", port_, attempt + 1);
    }
    log::warn("[native] giving up on the native helper -- using the Windows .exe path");
    return false;
}

void NativeHelper::stop() noexcept {
    std::scoped_lock lk{mu_};
    if (keepalive_ != INVALID_SOCKET) {
        closesocket(keepalive_); // helper kills its children and exits
        keepalive_ = INVALID_SOCKET;
    }
    if (wsa_) {
        WSACleanup();
        wsa_ = false;
    }
}

bool NativeHelper::alive() const noexcept {
    std::scoped_lock lk{mu_};
    if (keepalive_ == INVALID_SOCKET) return false;
    char b;
    int n = recv(keepalive_, &b, 1, MSG_PEEK);
    return n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK;
}

SOCKET NativeHelper::connect_send(const char* op, const std::vector<Stage>& chain,
                                  DWORD timeout_ms) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return s;
    // Room for ~5 s of 48 kHz s16 stereo, like the 1 MB pipes on the .exe path,
    // so a prefetched track can buffer while the current one plays.
    int rcvbuf = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvbuf), sizeof(rcvbuf));
    set_recv_timeout(s, timeout_ms);

    sockaddr_in sa{};
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(port_);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    std::string req = "FH2N/1\n" + token_ + "\n" + op + "\n" + std::to_string(chain.size()) + "\n";
    for (const auto& st : chain) {
        req += st.tool;
        req += '\n';
        req += std::to_string(st.args.size());
        req += '\n';
        for (const auto& a : st.args) append_field(req, a);
    }
    if (!send_all(s, req)) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

SOCKET NativeHelper::open_run(const std::vector<Stage>& chain) {
    if (!alive()) return INVALID_SOCKET;
    SOCKET s = connect_send("run", chain, 0);
    if (s != INVALID_SOCKET) set_nonblocking(s, true);
    return s;
}

std::string NativeHelper::run_capture(const Stage& stage, DWORD timeout_ms) {
    if (!alive()) return {};
    SOCKET s = connect_send("run", {stage}, timeout_ms);
    if (s == INVALID_SOCKET) return {};
    std::string out;
    char buf[1 << 14];
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (;;) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0 || GetTickCount64() > deadline) break;
        out.append(buf, (std::size_t)n);
    }
    closesocket(s);
    return out;
}

int sock_read(SOCKET s, void* buf, int len) noexcept {
    int n = recv(s, static_cast<char*>(buf), len, 0);
    if (n > 0) return n;
    if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) return 0;
    return -1;
}

} // namespace fh6::native
