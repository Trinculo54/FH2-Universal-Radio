// Shared by the FH2 mods (FH2 Universal Radio, WheresMyBuddy). Keep the
// copies in each repo identical. See fh2_updates.hpp.

#include "fh2_updates.hpp"
#include "fh2_strings.hpp"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <thread>
#include <vector>

namespace fh2common {

namespace fs = std::filesystem;

namespace {

// IDS_To_Start in Main.str: "TO START" in "PRESS (A) TO START".
constexpr uint16_t kToStartKey      = 0x18F0;
constexpr long long kCheckEverySecs = 6 * 60 * 60;

std::string narrow(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

std::string strip_v(std::string_view v) {
    while (!v.empty() && (v.front() == 'v' || v.front() == 'V')) v.remove_prefix(1);
    return std::string{v};
}

// GET over HTTPS; empty on failure.
std::string https_get(const std::wstring& host, const std::wstring& path, const std::wstring& agent) {
    std::string body;
    HINTERNET s = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return body;
    WinHttpSetTimeouts(s, 5000, 5000, 8000, 8000);
    HINTERNET c = WinHttpConnect(s, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                    : nullptr;
    DWORD status = 0, len = sizeof(status);
    if (r &&
        WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1L),
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, nullptr) &&
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &len, WINHTTP_NO_HEADER_INDEX) &&
        status == 200) {
        for (;;) {
            DWORD avail = 0, got = 0;
            if (!WinHttpQueryDataAvailable(r, &avail) || !avail || body.size() > (1 << 20)) break;
            std::string chunk(avail, '\0');
            if (!WinHttpReadData(r, chunk.data(), avail, &got) || !got) break;
            body.append(chunk.data(), got);
        }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return body;
}

// "tag_name": "v1.2.3" out of GitHub's release JSON.
std::string tag_from_json(const std::string& j) {
    const auto k = j.find("\"tag_name\"");
    if (k == std::string::npos) return {};
    const auto q1 = j.find('"', j.find(':', k) + 1);
    const auto q2 = q1 == std::string::npos ? q1 : j.find('"', q1 + 1);
    if (q2 == std::string::npos || q2 - q1 > 64) return {};
    return j.substr(q1 + 1, q2 - q1 - 1);
}

fs::path notices_dir(const fs::path& azimuth) { return azimuth / "fh2-mods" / "notices"; }

// A notice file: line 1 the mod's DLL (so a removed mod's notice is
// ignored), then one line per message.
void write_notice(const ModInfo& m, const std::vector<std::string>& lines) {
    std::error_code ec;
    const auto f = notices_dir(m.azimuth_dir) / (m.name + L".txt");
    if (lines.empty()) {
        fs::remove(f, ec);
        return;
    }
    fs::create_directories(f.parent_path(), ec);
    std::ofstream out{f, std::ios::binary | std::ios::trunc};
    out << narrow(m.dll) << "\n";
    for (const auto& l : lines) out << l << "\n";
}

struct Cache {
    long long checked = 0;
    std::string latest;
};

Cache read_cache(const ModInfo& m) {
    Cache c;
    std::ifstream in{notices_dir(m.azimuth_dir) / (m.name + L".check")};
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.rfind("checked=", 0) == 0) c.checked = std::atoll(line.c_str() + 8);
        if (line.rfind("latest=", 0) == 0) c.latest = line.substr(7);
    }
    return c;
}

void write_cache(const ModInfo& m, const Cache& c) {
    std::error_code ec;
    fs::create_directories(notices_dir(m.azimuth_dir), ec);
    std::ofstream{notices_dir(m.azimuth_dir) / (m.name + L".check"), std::ios::trunc}
        << "checked=" << c.checked << "\nlatest=" << c.latest << "\n";
}

// Short lines: the title screen has room for about 40 characters.
std::vector<std::string> compose(const ModInfo& m, const std::string& latest, bool loader_old) {
    std::vector<std::string> lines;
    if (!latest.empty() && compare_versions(latest, m.version) > 0)
        lines.push_back(narrow(m.name) + " " + strip_v(latest) + " is available");
    if (loader_old) lines.push_back("Old FH2 Mod Loader: reinstall " + narrow(m.name));
    return lines;
}

void run(ModInfo m) {
    auto say = [&](const std::string& s) {
        if (m.log) m.log(s);
    };
    const std::string have_loader = installed_loader_version(m.azimuth_dir);
    const bool loader_old         = compare_versions(have_loader, m.loader_version) < 0;
    say(narrow(m.name) + " " + m.version + ", FH2 Mod Loader " + have_loader +
        (loader_old ? " (older than the " + m.loader_version + " this mod ships with)" : ""));

    // Show what we already know straight away; the game reads its strings
    // early.
    Cache cache = read_cache(m);
    auto shown = compose(m, cache.latest, loader_old);
    write_notice(m, shown);
    refresh_title_notices(m.azimuth_dir);

    if (!m.check_online) return;
    const long long now = static_cast<long long>(std::time(nullptr));
    if (now - cache.checked >= 0 && now - cache.checked < kCheckEverySecs && !cache.latest.empty()) {
        if (compare_versions(cache.latest, m.version) > 0) say("update available: " + strip_v(cache.latest));
        return;
    }
    const auto json = https_get(L"api.github.com", L"/repos/" + widen(m.repo) + L"/releases/latest",
                                m.name + L"/" + widen(m.version));
    const auto tag = tag_from_json(json);
    if (tag.empty()) {
        say("couldn't check GitHub for updates this time");
        return;
    }
    cache.checked = now;
    cache.latest  = tag;
    write_cache(m, cache);
    if (compare_versions(tag, m.version) > 0)
        say("update available: " + strip_v(tag) + " (github.com/" + m.repo + "/releases)");
    else
        say("up to date (latest release " + strip_v(tag) + ")");
    const auto now_text = compose(m, tag, loader_old);
    if (now_text != shown) {
        write_notice(m, now_text);
        refresh_title_notices(m.azimuth_dir);
    }
}

} // namespace

int compare_versions(std::string_view a, std::string_view b) {
    const std::string x = strip_v(a), y = strip_v(b);
    std::size_t i = 0, j = 0;
    for (int part = 0; part < 4; ++part) {
        long long p = 0, q = 0;
        while (i < x.size() && x[i] >= '0' && x[i] <= '9') p = p * 10 + (x[i++] - '0');
        while (j < y.size() && y[j] >= '0' && y[j] <= '9') q = q * 10 + (y[j++] - '0');
        if (p != q) return p < q ? -1 : 1;
        if (i < x.size() && x[i] == '.') ++i;
        if (j < y.size() && y[j] == '.') ++j;
    }
    return 0;
}

std::string installed_loader_version(const fs::path& azimuth_dir) {
    const auto f   = (azimuth_dir / "d3dcompiler_47.dll").wstring();
    DWORD ignored  = 0;
    const DWORD sz = GetFileVersionInfoSizeW(f.c_str(), &ignored);
    if (!sz) return "1.0.0";
    std::vector<char> buf(sz);
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len             = 0;
    if (!GetFileVersionInfoW(f.c_str(), 0, sz, buf.data()) ||
        !VerQueryValueW(buf.data(), L"\\", reinterpret_cast<void**>(&fi), &len) || !fi)
        return "1.0.0";
    return std::to_string(HIWORD(fi->dwFileVersionMS)) + "." + std::to_string(LOWORD(fi->dwFileVersionMS)) + "." +
           std::to_string(HIWORD(fi->dwFileVersionLS));
}

void refresh_title_notices(const fs::path& azimuth_dir) {
    // Both mods may do this at the same moment.
    HANDLE mu = CreateMutexW(nullptr, FALSE, L"Local\\FH2ModNotices");
    if (mu) WaitForSingleObject(mu, 10000);

    std::error_code ec;
    std::vector<std::pair<std::wstring, std::vector<std::string>>> notes; // sorted by mod name
    for (const auto& e : fs::directory_iterator(notices_dir(azimuth_dir), ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != L".txt") continue;
        std::ifstream in{e.path()};
        std::string dll, text;
        std::getline(in, dll);
        dll = trim(dll);
        if (!dll.empty() && !fs::exists(azimuth_dir / "fh2-mods" / widen(dll), ec)) continue; // mod removed
        std::vector<std::string> lines;
        while (std::getline(in, text))
            if (!(text = trim(text)).empty()) lines.push_back(text);
        if (!lines.empty()) notes.emplace_back(e.path().stem().wstring(), lines);
    }
    std::sort(notes.begin(), notes.end());

    // "TO START" is its own text box beside "PRESS (A)", centred vertically
    // on it. Blank lines above balance the notice lines below so it stays
    // level with the button and the notices stack underneath; one extra
    // blank line evens out the box sitting a little high (checked in game).
    std::wstring above, below;
    for (const auto& [_, lines] : notes)
        for (const auto& l : lines) {
            above += L"\n";
            below += L"\n" + widen(l);
        }
    if (!below.empty()) above += L"\n";

    const auto mount = find_game_mount(azimuth_dir);
    if (!mount.empty()) {
        override_string_table(
            mount, "Main.str",
            [&](uint16_t key, const std::wstring& t) { return key == kToStartKey ? above + t + below : t; },
            below.empty());
    }
    if (mu) {
        ReleaseMutex(mu);
        CloseHandle(mu);
    }
}

void start_update_check(ModInfo info) {
    std::thread([m = std::move(info)] { run(m); }).detach();
}

} // namespace fh2common
