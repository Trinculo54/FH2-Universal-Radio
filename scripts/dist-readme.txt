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
24 Nov 2015) running under Azimuth. The radio is an FH2ML mod, so it works
alongside the other FH2ML mods, like WheresMyBuddy.


Requirements
~~~~~~~~~~~~

  FH2ML, the mod loader for Azimuth:
      https://github.com/Refilewski/FH2ML
  Forza Horizon 2 Extra Options:
      https://github.com/RokkuDayo/forzahorizon2_extraoptions


Install
~~~~~~~

1. Close Azimuth.
2. Install the zip with the FH2ML Manager (Install mod, then pick the
   zip), or extract it into the Azimuth folder, the one with azimuth.exe.
   Either way the radio lands in mods\universalradio:
       mod.json                  (tells FH2ML about the mod)
       FH2UniversalRadio.dll     (the radio)
       fh2-radio-worker.exe      (starts librespot, ffmpeg and yt-dlp)
       ui\                       (the dashboard)
       config.example.toml       (the starting point for config.toml)
3. Start Azimuth and FH2. In Settings > Audio, turn Radio DJ off.
4. Tune the radio to HORIZON PULSE (station 1). That station now
   plays the mod's audio. Every other station stays vanilla.
5. Open http://localhost:8420 in a browser for the dashboard.

Your files -- fh2.ini, config.toml, the Spotify login, the logs and the
helpers the mod downloads -- live in FH2ML\data\universalradio, in the
Azimuth folder. Updating or reinstalling the mod never touches them.

Updating from 1.7.x or older: install FH2ML and this zip, then let the
FH2ML Manager remove the old mod loader those versions came with (its Mods
page finds it and offers to); FH2ML doesn't start the radio while that's
still there. On the first launch the radio copies your settings, Spotify
login and downloaded helpers from the old fh2-radio folder into
FH2ML\data\universalradio, once. After that fh2-radio isn't used, and you
can delete it.

Windows Firewall may ask about azimuth.exe / librespot.exe the first time:
allow private networks so your phone can see the Spotify device.


Spotify
~~~~~~~

Spotify is the default source. On first launch the mod downloads
librespot and ffmpeg into FH2ML\data\universalradio\bin (takes a few
seconds). Then, on your phone or PC (same Wi-Fi), open Spotify, tap the
Devices icon and pick "FH2 Universal Radio". Requires Spotify Premium (a
Spotify Connect rule). Credentials are cached in
FH2ML\data\universalradio\spotify_cache, so it reconnects next time.

You control playback from the Spotify app as usual; skipping, pausing and
playlists all work.


FH2 settings (FH2ML\data\universalradio\fh2.ini)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Written on first launch; edits apply live (switch the in-game station
away and back to apply immediately). The on/off switches below are also
in the game, under Pause > Settings > MODS.

  station=1         which station is taken over (1-7, 0 = all of them)
                      1 Horizon Pulse     2 Bass Arena       3 Horizon XS
                      4 Radio Levante     5 Hospital Records 6 Ninja Tune
                      7 Innovative Leisure
  enabled=1         0 = all stations vanilla
  mute_dj=1         silence the DJ / jingles on that station
  extend_tracks=1   stretch the game's "track" to 16 min so it changes
                    (with a tiny hiccup) far less often than every 3-6 min
  show_metadata=1   show your song title / artist in the radio HUD

When a newer version is out, the title screen says so after "PRESS (A)
TO START"; FH2ML checks GitHub for it. To turn that off, set
"updateChecks": false in FH2ML\config\FH2ML.json (every mod), or
"mods": {"universalradio": {"updateChecks": false}} (just this one). The
old [updates] check= line in fh2.ini isn't read any more.


Troubleshooting
~~~~~~~~~~~~~~~

Everything is logged to FH2ML\data\universalradio\bridge.log.
FH2ML\logs\FH2ML.log shows whether FH2ML loaded the radio at all
("universalradio: lifecycle v4 initialized"), and if the game crashes, the
report in FH2ML\logs\crashes says which mod, if any, was involved. Useful
lines in bridge.log:

  [fh2] found FH2 image ...           the mod saw the game start
  [fh2] radio hooks installed ...     the radio is patched
  [fh2] load #N: track station=1 ...  the game loaded a song; "TAKEOVER"
                                      means it's playing your audio

No sound on Horizon Pulse: check the dashboard shows Spotify "playing" and
that you've picked "FH2 Universal Radio" in the Spotify app. If the log
never says "radio hooks installed", send the logs over.

Uninstall: remove the radio in the FH2ML Manager, or delete
mods\universalradio. Your settings stay in FH2ML\data\universalradio;
delete that folder too to remove them.


License
~~~~~~~

GPLv3 (LICENSE.txt), same as FH6 Universal Radio. Source is included
alongside this build. Unofficial fan-made mod; not affiliated with
Playground Games, Turn 10, Microsoft, or Spotify.
