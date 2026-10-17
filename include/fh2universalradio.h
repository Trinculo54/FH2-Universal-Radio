/*
 * fh2universalradio.h -- what FH2 Universal Radio offers other FH2 mods
 * (MIT, like the FH2 Mod Platform API).
 *
 * FH2 Universal Radio publishes this through the FH2 Mod Platform (API 2
 * and up). From your mod, on your own thread:
 *
 *     if (api->api_version >= 2 && api->wait_for_mods(30000)) {
 *         const FH2RadioNowPlayingV1* radio = (const FH2RadioNowPlayingV1*)
 *             api->get_interface(FH2_RADIO_NOW_PLAYING, 1);
 *         if (radio) { ... }
 *     }
 *
 * It's updated twice a second. Every function is thread-safe. Members are
 * only ever added at the end; check `size`.
 */
#ifndef FH2UNIVERSALRADIO_H
#define FH2UNIVERSALRADIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FH2_RADIO_NOW_PLAYING "FH2UniversalRadio.NowPlaying"

typedef struct FH2RadioNowPlayingV1 {
    uint32_t size; /* sizeof(FH2RadioNowPlayingV1) as the radio knows it */

    /* Returns 1 if one of the radio's sources is playing and the game is
       listening (the player is tuned to a replaced station), else 0. Copies
       the song title, artist and source name ("Spotify", a station's name)
       as UTF-8 into whichever buffers aren't NULL; they're empty when
       nothing is known. */
    int (*now_playing)(char* title, int title_size, char* artist, int artist_size, char* source,
                       int source_size);
} FH2RadioNowPlayingV1;

#ifdef __cplusplus
}
#endif

#endif /* FH2UNIVERSALRADIO_H */
