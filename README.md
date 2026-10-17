<h1 align="center" id="title">FH2 Universal Radio</h1>

An open-source radio mod for **Forza Horizon 2** on PC. It takes over one of the in-game radio stations and feeds it from **Spotify**, your **local music**, **online radio** stations, **YouTube Music**, a **Jellyfin** server, or **any Windows app**, controlled from a browser dashboard.

This is a port of [FH6 Universal Radio](https://github.com/g0ldyy/fh6-universal-radio) by [g0ldyy](https://github.com/g0ldyy) and [contributors](https://github.com/g0ldyy/fh6-universal-radio/graphs/contributors). Their mod does all the heavy lifting here: every audio source, the playback engine and the dashboard come straight from it. This port only adds the part that hooks into Forza Horizon 2 instead of Forza Horizon 6, so if you like it, go give the original a star.

## Features

The music plays through FH2's own radio, so it behaves like any other station. It ducks in menus, pauses with the game, follows your in-game radio volume, and even plays out of the festival speakers when you drive past the festival. The station's HUD shows the title and artist of whatever you're actually listening to, and the DJ and station jingles are silenced on the taken-over station.

Only one station is changed, Horizon Pulse by default, since it's the first one you unlock. Every other station stays exactly as it was. Everything the FH6 mod can play works here too: Spotify Connect, local files, online radio from thousands of stations, YouTube Music links, Jellyfin playlists, and audio captured from any app through a virtual audio cable. The dashboard runs at `http://localhost:8420`.

It also works alongside [Forza Horizon 2 Extra Options](https://github.com/RokkuDayo/forzahorizon2_extraoptions) and [WheresMyBuddy](https://github.com/Trinculo54/WheresMyBuddy). Extra Options loads through `winmm.dll`, and this mod and WheresMyBuddy load through the FH2 Mod Loader, so none of them get in each other's way.

## Install

You'll need Forza Horizon 2 running on PC through Azimuth, which is the usual way people play it.

Download the latest zip from the [Releases](../../releases) page, close Azimuth, and extract the zip into your Azimuth folder, the one with `azimuth.exe` in it. You should end up with `d3dcompiler_47.dll` and the `fh2-mods` and `fh2-radio` folders sitting right next to `azimuth.exe`, with `FH2UniversalRadio.dll` inside `fh2-mods`.

Launch the game as normal. The first time, the mod downloads a couple of helper programs into `fh2-radio\bin`, which only takes a few seconds. If Windows Firewall asks about `azimuth.exe` or `librespot.exe`, allow it on private networks so your phone can see the Spotify device. Once you're in game, go to **Settings > Audio** and set **Radio DJ** to Off, then tune to **Horizon Pulse**.

Open <http://localhost:8420> in any browser on the same machine to get to the dashboard. From another device on your network, use your PC's local IP instead (for example `http://192.168.1.42:8420`). Run `ipconfig` in a Command Prompt to find it.

### Updating from 1.0.x

Extract the new zip over the old install and let it replace `d3dcompiler_47.dll`. In 1.0.x that file was the whole mod. From 1.1.0 it's the FH2 Mod Loader, a tiny DLL whose only job is to load every mod in the `fh2-mods` folder, and the radio itself lives in `fh2-mods\FH2UniversalRadio.dll`. Your settings, Spotify login and downloaded helpers in `fh2-radio` are kept.

### Using it with other FH2 mods

Every mod built on the FH2 Mod Loader ships the same `d3dcompiler_47.dll`, so you can install them in any order and let them overwrite it. Each one just drops its own DLL into `fh2-mods`. [WheresMyBuddy](https://github.com/Trinculo54/WheresMyBuddy), which puts your Xbox friends' names on the Drivatars, is one of them. If there's no `fh2-mods\loader.log` after you launch the game, something has replaced the loader, such as an old 1.0.x radio zip. Copy `d3dcompiler_47.dll` from this zip back next to `azimuth.exe`.

### Spotify Connect

Spotify is the default source. Open the Spotify app on a phone or PC on the same network, tap the **Devices** icon and pick **FH2 Universal Radio**. From then on you control playback from Spotify like you normally would. Your login is saved in `fh2-radio\spotify_cache`, so it reconnects on its own next time. This needs Spotify Premium, which is a Spotify Connect requirement.

### Dependencies

Spotify, online radio, YouTube Music, Jellyfin and some local file formats rely on `librespot`, `ffmpeg` and `yt-dlp`. The mod downloads these automatically into `fh2-radio\bin` on first launch, so there's nothing to install by hand. If you'd rather manage them yourself, set their paths in the dashboard settings.

### Choosing the station

The FH2-specific settings are in `fh2-radio\fh2.ini`, which is created the first time you launch the game. `station` picks which station plays your music, using the in-game order: 1 Horizon Pulse, 2 Bass Arena, 3 Horizon XS, 4 Radio Levante, 5 Hospital Records, 6 Ninja Tune and 7 Innovative Leisure. Setting it to 0 takes over every station. Setting `enabled` to 0 turns the mod off and leaves every station vanilla.

The other options are on by default. `mute_dj` silences the DJ and jingles, `show_metadata` puts your song info in the HUD, and `extend_tracks` makes the game change its internal track every 16 minutes instead of every few, which keeps tiny hiccups in your music to a minimum. Changes are picked up while the game is running. Tune away from the station and back to apply them straight away.

Everything else, like sources, volume and local music folders, is set from the dashboard just like in FH6 Universal Radio.

### Updates

When a newer FH2 Universal Radio is out, the title screen tells you: "PRESS (A) TO START" gets a note like "FH2 Universal Radio 1.3.0 is available" added to it. The mod asks GitHub at most every six hours and remembers the answer between launches, and `check=0` under `[updates]` in `fh2.ini` turns that off. If another mod has put back an older FH2 Mod Loader than the one the radio came with, the title screen says that too, and reinstalling the radio fixes it. Other mods that use the loader, like WheresMyBuddy, put their notes in the same spot. The version you have is in the first line of `bridge.log` and in the DLL's file properties.

## Uninstall

Delete `fh2-mods\FH2UniversalRadio.dll` and the `fh2-radio` folder from your Azimuth folder. If you don't use any other FH2 mods, delete `d3dcompiler_47.dll` and the `fh2-mods` folder too.

## Build from source

The output is the FH2 Mod Loader (`d3dcompiler_47.dll`, built from `loader/`) plus the mod itself, `FH2UniversalRadio.dll`. It's built with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases), since the code uses MSVC-style SEH that only Clang supports among the free compilers. On Linux, with CMake and llvm-mingw installed:

```bash
./scripts/get-deps.sh   # one-time: header-only deps
./scripts/build.sh      # compile + stage dist/
```

`dist/` then holds `d3dcompiler_47.dll`, the `fh2-mods` and `fh2-radio` folders and a `README.txt`, ready to zip up. GitHub Actions does the same on every push and uploads the result as a build artifact. Pushing a tag like `v1.1.0` also creates a draft release with the zip attached, ready to be published. The version lives in `project(... VERSION x.y.z)` in `CMakeLists.txt`. Bump it before tagging, since the tag build refuses to run if the two don't match, and it's what the mod compares against the latest GitHub release. The code in `common/` is shared with WheresMyBuddy and should stay identical in both repos.

The loader is a `d3dcompiler_47.dll` proxy. Azimuth imports that DLL and Windows looks next to the exe first, so it gets loaded at startup without touching any of Azimuth's own files. Every export forwards to the real one in System32, and all the loader does is load `fh2-mods\*.dll` and log what it did to `fh2-mods\loader.log`.

The FH2-specific code is in `src/fh2/` and `include/fh2/`. FH2 links FMOD Studio statically and exports FMOD's low-level API by name. The mod redirects two calls inside the game's `RadioStreamFmod` class, `System::createSound` and `Sound::getSubSound`. For the chosen station, it hands the game a user PCM stream fed from the mod's audio buffer instead of the song from the sound bank, and the game's own Studio event and radio bus take it from there. There's also an offline test in `tests/` that maps the real FH2 executable and runs the hooks against a fake FMOD under Wine.

## Troubleshooting

Everything gets logged to `fh2-radio\bridge.log`, and the FH2 Mod Loader writes `fh2-mods\loader.log`, which shows whether the radio was loaded at all. If the mod is working, you'll see `radio hooks installed` shortly after the game starts, and a line ending in `TAKEOVER` whenever the game loads a song on your station.

| Symptom | Fix |
|---|---|
| Dashboard doesn't open | Make sure `d3dcompiler_47.dll` is next to `azimuth.exe` (not inside `data\bin`) and `FH2UniversalRadio.dll` is inside `fh2-mods`, then restart Azimuth. `fh2-mods\loader.log` should list it as loaded. |
| FH2 Universal Radio doesn't show up in Spotify | Check you're on the same network, that the firewall allowed private networks, and that the account has Premium. Give librespot a few seconds to download on the first launch. |
| Station still plays the original songs | Check that `station` in `fh2.ini` matches the station you're listening to, and that the log says `radio hooks installed`. Tune away and back. |
| Station is silent | Make sure something is actually playing in Spotify (or your chosen source) and that the dashboard shows it as playing. |
| DJ still talks over the music | Set Radio DJ to Off in the game's audio settings. |

If you're still stuck, open an issue and attach your `bridge.log`.

## Why this exists

FH6 Universal Radio is a great mod, and I wanted the same thing while replaying Forza Horizon 2. FH2 turned out to use the same FMOD-based radio setup as FH6, even down to the same `RadioStreamFmod` class, so porting it was possible without rewriting the parts that matter. All credit for the mod itself goes to g0ldyy and the FH6 Universal Radio contributors.

## Support the Project

The best way to support this is to support the original. Star [FH6 Universal Radio](https://github.com/g0ldyy/fh6-universal-radio), and if it's made your drives better, consider donating to g0ldyy through [GitHub Sponsors](https://github.com/sponsors/g0ldyy) or [Ko-fi](https://ko-fi.com/g0ldyy). Bug reports and pull requests for the FH2 side are welcome here.

## License

Released under the [GNU General Public License v3.0](LICENSE), the same as FH6 Universal Radio. As a derivative of that project it stays GPLv3 and credits the original.

## Disclaimer

Unofficial fan-made mod. Not affiliated with, endorsed by, or connected to the FH6 Universal Radio authors, Playground Games, Turn 10 Studios, Xbox Game Studios, Microsoft, the Azimuth developers, Google, YouTube, Jellyfin (Jellyfin LLC), or Spotify (Spotify AB). All trademarks belong to their respective owners. Use at your own risk.
