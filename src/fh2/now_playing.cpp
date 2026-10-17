#include "fh2/now_playing.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>

namespace fh2 {

namespace {

std::mutex g_mu;
bool g_playing = false;
std::string g_source, g_title, g_artist;

void copy_out(const std::string& s, char* out, int size) {
    if (!out || size <= 0) return;
    const auto n = std::min<std::size_t>(s.size(), static_cast<std::size_t>(size - 1));
    std::memcpy(out, s.data(), n);
    out[n] = '\0';
}

int now_playing(char* title, int title_size, char* artist, int artist_size, char* source, int source_size) {
    std::scoped_lock lk{g_mu};
    copy_out(g_title, title, title_size);
    copy_out(g_artist, artist, artist_size);
    copy_out(g_source, source, source_size);
    return g_playing ? 1 : 0;
}

const FH2RadioNowPlayingV1 g_iface{sizeof(FH2RadioNowPlayingV1), now_playing};

} // namespace

void set_now_playing(bool playing, const std::string& source, const std::string& title, const std::string& artist) {
    std::scoped_lock lk{g_mu};
    g_playing = playing;
    if (g_source != source) g_source = source;
    if (g_title != title) g_title = title;
    if (g_artist != artist) g_artist = artist;
}

const FH2RadioNowPlayingV1* now_playing_interface() noexcept { return &g_iface; }

} // namespace fh2
