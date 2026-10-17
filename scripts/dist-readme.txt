FH2 Universal Radio
===================

A Forza Horizon 2 (Azimuth) port of FH6 Universal Radio by g0ldyy and
contributors (GPLv3, https://github.com/g0ldyy/fh6-universal-radio).

It takes over one of FH2's radio stations and plays your own audio on it:
Spotify (Spotify Connect), a folder of music files, online radio, YouTube
Music, Jellyfin, or any Windows app. The game treats it like a normal
station -- it ducks in menus, pauses with the game, follows the in-game
radio volume, and shows your song title/artist in the radio HUD.

Supported build: the Xbox One FH2 (forza_durango_release_final.exe,
24 Nov 2015) running under Azimuth. Works alongside Forza Horizon 2 Extra
Options (that mod uses winmm.dll; this one uses d3dcompiler_47.dll).


Install
~~~~~~~

1. Close Azimuth.
2. Copy d3dcompiler_47.dll and the fh2-radio folder next to azimuth.exe.
3. Start Azimuth and FH2. In Settings > Audio, turn Radio DJ off.
4. Tune the radio to HORIZON PULSE (station 1). That station now
   plays the mod's audio. Every other station stays vanilla.
5. Open http://localhost:8420 in a browser for the dashboard.

Windows Firewall may ask about azimuth.exe / librespot.exe the first time:
allow private networks so your phone can see the Spotify device.


Spotify
~~~~~~~

Spotify is the default source. On first launch the mod downloads
librespot and ffmpeg into fh2-radio\bin (takes a few seconds). Then, on
your phone or PC (same Wi-Fi), open Spotify, tap the Devices icon and pick
"FH2 Universal Radio". Requires Spotify Premium (a Spotify Connect rule).
Credentials are cached in fh2-radio\spotify_cache, so it reconnects next
time.

You control playback from the Spotify app as usual; skipping, pausing and
playlists all work.


FH2 settings (fh2-radio\fh2.ini)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Written on first launch; edits apply live (switch the in-game station
away and back to apply immediately).

  station=1         which station is taken over (1-7, 0 = all of them)
                      1 Horizon Pulse     2 Bass Arena       3 Horizon XS
                      4 Radio Levante     5 Hospital Records 6 Ninja Tune
                      7 Innovative Leisure
  enabled=1         0 = all stations vanilla
  mute_dj=1         silence the DJ / jingles on that station
  extend_tracks=1   stretch the game's "track" to 16 min so it changes
                    (with a tiny hiccup) far less often than every 3-6 min
  show_metadata=1   show your song title / artist in the radio HUD


Troubleshooting
~~~~~~~~~~~~~~~

Everything is logged to fh2-radio\bridge.log. Useful lines:

  [fh2] found FH2 image ...           the mod saw the game start
  [fh2] radio hooks installed ...     the radio is patched
  [fh2] load #N: track station=1 ...  the game loaded a song; "TAKEOVER"
                                      means it's playing your audio

No sound on Horizon Pulse: check the dashboard shows Spotify "playing" and
that you've picked "FH2 Universal Radio" in the Spotify app. If the log
never says "radio hooks installed", send the log over.

Uninstall: delete d3dcompiler_47.dll and the fh2-radio folder.


License
~~~~~~~

GPLv3, same as FH6 Universal Radio. Source is included alongside this
build. Unofficial fan-made mod; not affiliated with Playground Games,
Turn 10, Microsoft, or Spotify.
