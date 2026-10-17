<h1 align="center">📻 FH2 Universal Radio</h1>

<p align="center">
  <b>Play Spotify, your own music files, online radio and more on Forza Horizon 2's in-game radio.</b><br>
  A Forza Horizon 2 (PC / Azimuth) port of
  <a href="https://github.com/g0ldyy/fh6-universal-radio"><b>FH6 Universal Radio</b></a> by
  <a href="https://github.com/g0ldyy">g0ldyy</a> and contributors.
</p>

---

> [!IMPORTANT]
> **This project is built on [FH6 Universal Radio](https://github.com/g0ldyy/fh6-universal-radio)**
> by [g0ldyy](https://github.com/g0ldyy) and
> [contributors](https://github.com/g0ldyy/fh6-universal-radio/graphs/contributors).
> All of the audio sources (Spotify Connect, local files, online radio, YouTube Music, Jellyfin,
> external audio), the playback engine and the web dashboard are their work. This port only adds
> what's needed to hook into Forza Horizon 2 instead of Forza Horizon 6.
>
> If you enjoy it, please ⭐ [the original repo](https://github.com/g0ldyy/fh6-universal-radio) and
> consider supporting the original author via
> [GitHub Sponsors](https://github.com/sponsors/g0ldyy) or [Ko-fi](https://ko-fi.com/g0ldyy).

## Features

- **Spotify Connect**: pick "FH2 Universal Radio" in the Spotify app and it plays on the in-game radio (Spotify Premium required).
- **Every other FH6 Universal Radio source**: local music folders, online radio stations, YouTube Music, Jellyfin, or any Windows app.
- **Feels native**: the music runs through the game's own radio, so it ducks in menus, pauses with the game, follows the in-game radio volume, and plays from the festival speakers when you're near the festival.
- **Song title and artist in the radio HUD**: the game's now-playing display shows what you're actually listening to.
- **No DJ chatter or jingles** on the taken-over station.
- **Only one station is changed** (Horizon Pulse by default); every other station stays vanilla.
- **Web dashboard** at `http://localhost:8420` for switching sources, transport controls and settings.
- **Works alongside [Forza Horizon 2 Extra Options](https://github.com/RokkuDayo/forzahorizon2_extraoptions)**.

## Requirements

- Forza Horizon 2 (Xbox One version) running on PC through **Azimuth**, which is the standard PC setup.
- Windows 10 or 11.
- For Spotify: a **Spotify Premium** account (a Spotify Connect requirement) and the Spotify app on a phone or PC on the same network.

## Installation

1. Download the latest release zip from the [Releases](../../releases) page.
2. Close Azimuth if it's running.
3. Extract the zip into your **Azimuth folder** (the one containing `azimuth.exe`). Afterwards it should look like this:
   ```
   Azimuth\
   ├── azimuth.exe
   ├── d3dcompiler_47.dll    <- from this mod
   ├── fh2-radio\            <- from this mod
   ├── data\
   └── ...
   ```
4. Start Azimuth and launch Forza Horizon 2.
   - On first launch the mod downloads its helpers (librespot and ffmpeg) into `fh2-radio\bin`. This takes a few seconds.
   - If Windows Firewall asks about `azimuth.exe` or `librespot.exe`, allow access on **private networks** so your phone can find the Spotify device.
5. In the game, go to **Settings > Audio** and turn **Radio DJ off**.
6. Tune the radio to **Horizon Pulse**. That station now plays the mod's audio.

### Connecting Spotify

1. Make sure your phone or PC is on the same Wi-Fi/network as the gaming PC.
2. Open Spotify, start playing something, tap the **Devices** icon and choose **FH2 Universal Radio**.
3. Control playback from Spotify as usual: skip, pause and playlists all work. Your login is cached in `fh2-radio\spotify_cache`, so it reconnects automatically next time.

### The dashboard

Open **http://localhost:8420** in a browser while the game is running to switch sources, control playback, set up local music folders, add online radio stations and change settings. From another device on your network, use the PC's local IP instead (e.g. `http://192.168.1.42:8420`; run `ipconfig` to find it).

## Settings

FH2-specific settings live in `fh2-radio\fh2.ini`, which is created on first launch. Changes apply while the game is running; to apply one immediately, tune away from the station and back.

| Setting | Default | What it does |
|---|---|---|
| `station` | `1` | Which station plays the mod's audio. `0` = every station. |
| `enabled` | `1` | `0` turns the takeover off; every station plays vanilla. |
| `mute_dj` | `1` | Silences the DJ and jingles on the taken-over station. |
| `extend_tracks` | `1` | Makes the game change its internal "track" only every 16 minutes instead of every 3–6, so there are far fewer tiny hiccups in your music. |
| `show_metadata` | `1` | Shows your song title and artist in the radio HUD. |

Station numbers:

| # | Station | # | Station |
|---|---|---|---|
| 1 | Horizon Pulse | 5 | Hospital Records |
| 2 | Bass Arena | 6 | Ninja Tune |
| 3 | Horizon XS | 7 | Innovative Leisure |
| 4 | Radio Levante | | |

Everything else (sources, volume, Spotify, local files, etc.) is set from the dashboard and stored in `fh2-radio\config.toml`, exactly as in FH6 Universal Radio.

## Troubleshooting

Everything is logged to `fh2-radio\bridge.log`. Lines to look for:

- `[fh2] radio hooks installed`: the mod found the game and is active.
- `[fh2] load #N: track station=1 ... -> TAKEOVER`: the game loaded a song on your station and the mod took it over.

| Problem | Fix |
|---|---|
| The dashboard doesn't open | Make sure `d3dcompiler_47.dll` is next to `azimuth.exe` (not in `data\bin`), then restart Azimuth. |
| "FH2 Universal Radio" doesn't appear in Spotify | Same network? Firewall allowed on private networks? Spotify Premium? Wait a few seconds after launch for librespot to download. |
| The station still plays the original songs | Check that `station=` in `fh2.ini` matches the station you're on, and that the log says `radio hooks installed`. Tune away and back. |
| Silence on the station | Check the dashboard shows Spotify (or your source) as playing, and that something is actually playing in the Spotify app. |
| The DJ still talks | Turn Radio DJ off in the game's audio settings. |

Still stuck? Open an issue and attach `fh2-radio\bridge.log`.

## Uninstall

Delete `d3dcompiler_47.dll` and the `fh2-radio` folder from your Azimuth folder.

## How it works

For the curious. The game-specific code lives in `src/fh2/` and `include/fh2/`.

- **Loading:** `d3dcompiler_47.dll` is a proxy. `azimuth.exe` loads it at startup, and every export is forwarded to the real Windows DLL, so Azimuth's shader compiling is untouched. Extra Options uses `winmm.dll`, so the two don't clash.
- **Finding the game:** Azimuth maps the Xbox One executable into its own process. The mod scans for that image and checks the build's PE header and export table before touching anything.
- **Hooking:** FH2 links FMOD Studio statically and exports FMOD's low-level API by name. The mod redirects two calls inside the game's `RadioStreamFmod` class (`System::createSound` and `Sound::getSubSound`). For the chosen station's songs it hands the game an `FMOD_OPENUSER` PCM stream fed from the mod's audio buffer, instead of the song from the sound bank. The game's own Studio event, radio bus, ducking, pause and volume stay in charge of everything else.
- **Track length:** the game reloads any radio stream that drifts more than 5 s from the station's clock, and ends tracks at their XML length. The mod stretches the taken-over tracks to 16 minutes, within the range the game already supports.
- **HUD:** the song title and artist are written into the track's `SampleProperties` strings, which the radio HUD reads.

## Building from source

Requires [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) (Clang, for MSVC-style SEH) and CMake. On Linux:

```bash
./scripts/get-deps.sh   # one-time: header-only dependencies
./scripts/build.sh      # compiles + stages everything in dist/
```

`dist/` then contains `d3dcompiler_47.dll`, the `fh2-radio` folder and `README.txt`, ready to zip.

There's also an offline test that maps the real FH2 executable and drives the hooks with a fake FMOD (it runs under Wine):

```bash
cmake -S . -B build-test -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DFH2_TESTS=ON
cmake --build build-test --target fh2_hook_test
wine build-test/fh2_hook_test.exe forza_durango_release_final.exe RadioInfo_EN.xml
```

## Credits

- **[FH6 Universal Radio](https://github.com/g0ldyy/fh6-universal-radio)** by **[g0ldyy](https://github.com/g0ldyy)** and [contributors](https://github.com/g0ldyy/fh6-universal-radio/graphs/contributors): the project this is a port of, including every audio source, the dashboard and the playback engine. The first-launch helper downloads (librespot, ffmpeg) also come from its releases.
- **[librespot](https://github.com/librespot-org/librespot)**: Spotify Connect.
- **[Forza Horizon 2 Extra Options](https://github.com/RokkuDayo/forzahorizon2_extraoptions)** by RokkuDayo: a useful reference for how Azimuth loads the game.
- The Azimuth team, for making FH2 playable on PC.

## License

Released under the [GNU General Public License v3.0](LICENSE), the same license as FH6 Universal Radio. This is a derivative work; per the original project's terms it remains GPLv3 and credits the original.

## Disclaimer

Unofficial fan-made mod. Not affiliated with, endorsed by, or connected to the FH6 Universal Radio authors, Playground Games, Turn 10 Studios, Xbox Game Studios, Microsoft, the Azimuth developers, or Spotify. All trademarks belong to their respective owners. Use at your own risk.
