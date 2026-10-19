#include "fh6/subprocess.hpp"

#include <format>
#include <tlhelp32.h>

namespace fh6::subprocess {

namespace {

// Read one Environment\Path value from the registry, expanding %FOO% refs.
std::wstring read_registry_path(HKEY root, const wchar_t* subkey) {
    DWORD bytes           = 0;
    constexpr DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND;
    if (RegGetValueW(root, subkey, L"Path", flags, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t))
        return {};

    std::wstring raw(bytes / sizeof(wchar_t), L'\0');
    DWORD type = 0;
    if (RegGetValueW(root, subkey, L"Path", flags, &type, raw.data(), &bytes) != ERROR_SUCCESS)
        return {};
    while (!raw.empty() && raw.back() == L'\0') raw.pop_back();
    if (type != REG_EXPAND_SZ || raw.empty()) return raw;

    DWORD need = ExpandEnvironmentStringsW(raw.c_str(), nullptr, 0);
    if (need == 0) return raw;
    std::wstring expanded(need, L'\0');
    DWORD got = ExpandEnvironmentStringsW(raw.c_str(), expanded.data(), need);
    if (got == 0 || got > need) return raw;
    expanded.resize(got - 1); // strip terminating NUL
    return expanded;
}

// Rebuild this process' PATH from HKLM + HKCU. Used as a fallback after a
// CreateProcessW lookup miss.Returns true iff PATH actually changed.
bool refresh_path_from_registry() {
    auto sys = read_registry_path(
        HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
    auto usr = read_registry_path(HKEY_CURRENT_USER, L"Environment");
    if (sys.empty() && usr.empty()) return false;

    std::wstring merged = std::move(sys);
    if (!usr.empty()) {
        if (!merged.empty() && merged.back() != L';') merged += L';';
        merged += usr;
    }

    DWORD cur_len = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (cur_len > 1) {
        std::wstring current(cur_len, L'\0');
        DWORD written = GetEnvironmentVariableW(L"PATH", current.data(), cur_len);
        if (written > 0 && written < cur_len) {
            current.resize(written);
            if (current == merged) return false;
        }
    }
    return SetEnvironmentVariableW(L"PATH", merged.c_str()) != 0;
}

} // namespace

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

std::string narrow(std::wstring_view ws) {
    if (ws.empty()) return {};
    int n =
        WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring quote(const std::wstring& s) {
    if (s.empty()) return L"\"\"";
    if (s.find_first_of(L" \t\"") == std::wstring::npos) return s;
    std::wstring out{L"\""};
    for (auto c : s) {
        if (c == L'"') out += L'\\';
        out += c;
    }
    out += L'"';
    return out;
}

HANDLE open_nul(DWORD access) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE h = CreateFileW(L"NUL", access, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                           0, nullptr);
    return h == INVALID_HANDLE_VALUE ? nullptr : h;
}

namespace {
std::filesystem::path g_log_dir;
} // namespace

void set_log_directory(const std::filesystem::path& dir) { g_log_dir = dir; }

// Helper-process output (librespot, ffmpeg, yt-dlp). Lives in the mod's data
// folder (FH2ML\data\universalradio\helpers.log) so it's easy to find and
// send: the DLL sets it from FH2ML's data_directory, the worker from what the
// DLL hands it. Trimmed at first use each run if it grows past 4 MB.
std::filesystem::path stderr_log_path() {
    static const std::filesystem::path path = [] {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path dir = g_log_dir;
        if (dir.empty()) dir = fs::temp_directory_path(ec);
        fs::path p = dir / "helpers.log";
        if (fs::file_size(p, ec) > (4u << 20) && !ec) fs::remove(p, ec);
        return p;
    }();
    return path;
}

HANDLE open_stderr_log() {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE h = CreateFileW(stderr_log_path().wstring().c_str(), FILE_APPEND_DATA | SYNCHRONIZE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    return h == INVALID_HANDLE_VALUE ? open_nul(GENERIC_WRITE) : h;
}

const wchar_t* safe_spawn_cwd() {
    static const std::wstring dir = [] {
        wchar_t buf[MAX_PATH];
        UINT n = GetSystemDirectoryW(buf, MAX_PATH);
        return (n > 0 && n < MAX_PATH) ? std::wstring{buf, n} : std::wstring{};
    }();
    return dir.empty() ? nullptr : dir.c_str();
}

HANDLE create_kill_on_close_job() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

HANDLE spawn_in_job(HANDLE job, const std::wstring& cmd, HANDLE stdin_h, HANDLE stdout_h,
                    HANDLE stderr_h) {
    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = stdin_h;
    si.hStdOutput = stdout_h;
    si.hStdError  = stderr_h;

    PROCESS_INFORMATION pi{};
    std::wstring mut;
    auto launch = [&] {
        mut = cmd; // CreateProcessW may mutate the buffer; reset for the retry.
        return CreateProcessW(nullptr, mut.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, safe_spawn_cwd(), &si,
                              &pi) != 0;
    };
    if (!launch()) {
        // First attempt missed the binary. If it looks like a stale-PATH miss
        // (e.g. user just ran `winget install yt-dlp`), pull the live PATH
        // from the registry and try once more; otherwise propagate the error.
        const DWORD ec             = GetLastError();
        const bool stale_path_miss = (ec == ERROR_FILE_NOT_FOUND || ec == ERROR_PATH_NOT_FOUND) &&
                                     refresh_path_from_registry();
        if (!stale_path_miss || !launch()) {
            SetLastError(ec);
            return nullptr;
        }
    }
    if (job) {
        // Best-effort: Job Objects are not fully implemented under Wine/Proton.
        // If AssignProcessToJobObject fails (common under Wine), we continue
        // without job containment rather than killing the child process.
        // On native Windows this succeeds and gives us kill-on-close semantics.
        AssignProcessToJobObject(job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

std::string drain_to_eof(HANDLE pipe, std::size_t max_bytes) {
    std::string out;
    char buf[1 << 16];
    DWORD got = 0;
    // Read to EOF even past the cap so the writer never blocks on a full pipe;
    // bytes beyond max_bytes are dropped.
    while (ReadFile(pipe, buf, sizeof(buf), &got, nullptr) && got > 0) {
        if (max_bytes == 0) {
            out.append(buf, got);
        } else if (out.size() < max_bytes) {
            const std::size_t room = max_bytes - out.size();
            out.append(buf, got < room ? got : room);
        }
    }
    return out;
}

std::string capture_output(const std::wstring& cmd, bool capture_stderr, std::size_t max_bytes) {
    HANDLE job = create_kill_on_close_job();
    if (!job) return {};

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 1 << 16)) {
        CloseHandle(job);
        return {};
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

    // Capture the requested stream via wr; the other goes to NUL (stdout) or the
    // shared stderr log (stderr).
    HANDLE nul_in   = open_nul(GENERIC_READ);
    HANDLE off_band = capture_stderr ? open_nul(GENERIC_WRITE) : open_stderr_log();
    HANDLE proc     = spawn_in_job(job, cmd, nul_in, capture_stderr ? off_band : wr,
                                   capture_stderr ? wr : off_band);
    CloseHandle(wr);
    if (nul_in) CloseHandle(nul_in);
    if (off_band) CloseHandle(off_band);
    if (!proc) {
        CloseHandle(rd);
        CloseHandle(job);
        return {};
    }

    std::string out = drain_to_eof(rd, max_bytes);
    CloseHandle(rd);
    CloseHandle(proc);
    CloseHandle(job); // KILL_ON_JOB_CLOSE reaps the child if it hasn't exited
    return out;
}

namespace {

// Freeze every thread of `pid`. Called before we enumerate and terminate a
// subtree so the target (a PyInstaller yt-dlp can fork faster than we walk)
// cannot spawn new children mid-traversal, nor recycle a just-reaped PID.
void suspend_process(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        if (HANDLE th = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID)) {
            SuspendThread(th);
            CloseHandle(th);
        }
    }
    CloseHandle(snap);
}

} // namespace

void kill_process_tree(DWORD pid) {
    // Suspend first, then walk the snapshot we captured of its (now-frozen)
    // children before terminating it. The residual PID-reuse window at the very
    // leaf is inherent to PID-based termination and unavoidable.
    suspend_process(pid);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
            if (pe.th32ParentProcessID == pid) kill_process_tree(pe.th32ProcessID);
        }
        CloseHandle(snap);
    }

    if (HANDLE proc = OpenProcess(PROCESS_TERMINATE, FALSE, pid)) {
        TerminateProcess(proc, 1);
        CloseHandle(proc);
    }
}

void reap(HANDLE& proc) noexcept {
    if (!proc) return;
    if (DWORD pid = GetProcessId(proc)) {
        kill_process_tree(pid);
    } else {
        TerminateProcess(proc, 1); // PID lookup failed -- terminate just this one
    }
    CloseHandle(proc);
    proc = nullptr;
}

std::string describe_launch_failure(const std::wstring& bin, DWORD ec, bool from_config) {
    wchar_t resolved[MAX_PATH] = {};
    DWORD got = SearchPathW(nullptr, bin.c_str(), L".exe", MAX_PATH, resolved, nullptr);
    std::string where =
        got ? narrow({resolved, got})
            : (from_config ? "(configured path not found on disk)" : "(not found on PATH)");

    std::string sys_msg;
    LPWSTR raw = nullptr;
    DWORD len  = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, ec, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&raw, 0, nullptr);
    if (raw && len) {
        while (len && (raw[len - 1] == L'\r' || raw[len - 1] == L'\n' || raw[len - 1] == L' '))
            --len;
        sys_msg = narrow({raw, len});
    }
    if (raw) LocalFree(raw);

    const char* hint = "";
    switch (ec) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            hint = from_config
                     ? " -- the configured path does not exist. Fix it or clear it to fall "
                       "back to PATH lookup."
                     : " -- not on your PATH. Install it (winget install yt-dlp.yt-dlp / "
                       "Gyan.FFmpeg) or set the full .exe path in config.toml.";
            break;
        case ERROR_ACCESS_DENIED:
            hint = " -- likely blocked or quarantined by antivirus. Whitelist the binary and "
                   "the game folder.";
            break;
        case ERROR_BAD_EXE_FORMAT:
            hint = " -- not a valid Win64 executable. Download the Windows build, not the Linux "
                   "binary or a bare script.";
            break;
        case ERROR_SHARING_VIOLATION:
            hint = " -- another process has the file open (often AV scanning). Retry in a few "
                   "seconds.";
            break;
        default: break;
    }

    return std::format("ec={} ({}) tried={} resolved={}{}", ec,
                       sys_msg.empty() ? "unknown" : sys_msg, narrow(bin), where, hint);
}

} // namespace fh6::subprocess
