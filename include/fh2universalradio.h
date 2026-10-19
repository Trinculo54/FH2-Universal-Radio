/*
 * fh2universalradio.h -- what FH2 Universal Radio offers other FH2 mods
 * (MIT, like FH2ML's SDK headers).
 *
 * FH2 Universal Radio (mod ID "universalradio") publishes this from its
 * Initialize, as an FH2ML v4 interface (FH2ML_v4.h). From your v4 mod, on
 * your own thread (S being your FH2ML_Services_v4):
 *
 *     if (S->WaitForMods(30000)) {
 *         const FH2RadioNowPlayingV1* radio = (const FH2RadioNowPlayingV1*)
 *             S->GetInterface(FH2_RADIO_NOW_PLAYING, 1);
 *         if (radio) { ... }
 *     }
 *
 * (Or, if your mod can't do without the radio, list "universalradio" under
 * "dependencies" in your mod.json and look it up straight from your
 * Initialize.) It's updated twice a second. Every
 * function is thread-safe. Members are only ever added at the end; check
 * `size`.
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
