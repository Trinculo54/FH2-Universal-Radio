<h1 align="center" id="title">FH2 Universal Radio</h1>

An open-source radio mod for **Forza Horizon 2** on PC. It takes over one of the in-game radio stations and feeds it from **Spotify**, your **local music**, **online radio** stations, **YouTube Music**, a **Jellyfin** server, or **any Windows app**, controlled from a browser dashboard.

This is a port of [FH6 Universal Radio](https://github.com/g0ldyy/fh6-universal-radio) by [g0ldyy](https://github.com/g0ldyy) and [contributors](https://github.com/g0ldyy/fh6-universal-radio/graphs/contributors). Their mod does all the heavy lifting here: every audio source, the playback engine and the dashboard come straight from it. This port only adds the part that hooks into Forza Horizon 2 instead of Forza Horizon 6, so if you like it, go give the original a star.

## Features

The music plays through FH2's own radio, so it behaves like any other station. It ducks in menus, pauses with the game, follows your in-game radio volume, and even plays out of the festival speakers when you drive past the festival. The station's HUD shows the title and artist of whatever you're actually listening to, and the DJ and station jingles are silenced on the taken-over station.

Only one station is changed, Horizon Pulse by default, since it's the first one you unlock. Every other station stays exactly as it was. Everything the FH6 mod can play works here too: Spotify Connect, local files, online radio from thousands of stations, YouTube Music links, Jellyfin playlists, and audio captured from any app through a virtual audio cable. The dashboard runs at `http://localhost:8420`.

It's an [FH2ML](https://github.com/Refilewski/FH2ML) mod, so it works alongside [Forza Horizon 2 Extra Options](https://github.com/RokkuDayo/forzahorizon2_extraoptions), [WheresMyBuddy](https://github.com/Trinculo54/WheresMyBuddy) and the other FH2ML mods. Its hooks go through FH2ML, which keeps every mod's changes to the game from getting in each other's way.

## Install

You'll need Forza Horizon 2 running on PC through Azimuth, which is the usual way people play it, with [FH2ML](https://github.com/Refilewski/FH2ML) (the mod loader) and [Forza Horizon 2 Extra Options](https://github.com/RokkuDayo/forzahorizon2_extraoptions) installed.

Download the latest zip from the [Releases](../../releases) page and close Azimuth. In the FH2ML Manager, click **Install mod** and pick the zip. Or extract the zip into your Azimuth folder, the one with `azimuth.exe` in it. Either way the mod ends up in `mods\universalradio`, with `mod.json`, `FH2UniversalRadio.dll` and the dashboard's `ui` folder inside.

Your own files, `fh2.ini`, `config.toml`, the Spotify login, the logs and the downloaded helpers, live in `FH2ML\data\universalradio` in the Azimuth folder, and updating or reinstalling the mod never touches them.

Launch the game as normal. The first time, the mod downloads a couple of helper programs into `FH2ML\data\universalradio\bin`, which only takes a few seconds. If Windows Firewall asks about `azimuth.exe` or `librespot.exe`, allow it on private networks so your phone can see the Spotify device. Once you're in game, go to **Settings > Audio** and set **Radio DJ** to Off, then tune to **Horizon Pulse**.

Open <http://localhost:8420> in any browser on the same machine to get to the dashboard. From another device on your network, use your PC's local IP instead (for example `http://192.168.1.42:8420`). Run `ipconfig` in a Command Prompt to find it.

### Updating from 1.7 or older

Up to 1.7, the radio came with a mod loader of its own. Since 1.8.0 it's an FH2ML mod instead. Install FH2ML and the new zip as above, then let the FH2ML Manager remove the old loader: its Mods page finds it and offers to. FH2ML doesn't start the radio while the old loader is still there. On the first launch the radio copies your settings, Spotify login and downloaded helpers from the old `fh2-radio` folder into `FH2ML\data\universalradio`, once. After that `fh2-radio` isn't used, and you can delete it.

### Using it with other FH2 mods

FH2ML loads every mod in the `mods` folder, so you can install them in any order. [WheresMyBuddy](https://github.com/Trinculo54/WheresMyBuddy), which puts your Xbox friends' names on the Drivatars, is one of them. If the radio doesn't seem to load, `FH2ML\logs\FH2ML.log` says why; once it has, it has a `universalradio: lifecycle v4 initialized` line.

### Spotify Connect

Spotify is the default source. Open the Spotify app on a phone or PC on the same network, tap the **Devices** icon and pick **FH2 Universal Radio**. From then on you control playback from Spotify like you normally would. Your login is saved in `FH2ML\data\universalradio\spotify_cache`, so it reconnects on its own next time. This needs Spotify Premium, which is a Spotify Connect requirement.

### Dependencies

Spotify, online radio, YouTube Music, Jellyfin and some local file formats rely on `librespot`, `ffmpeg` and `yt-dlp`. The mod downloads these automatically into `FH2ML\data\universalradio\bin` on first launch, so there's nothing to install by hand. If you'd rather manage them yourself, set their paths in the dashboard settings.

### Choosing the station

The FH2-specific settings are in `FH2ML\data\universalradio\fh2.ini`, which is created the first time you launch the game. `station` picks which station plays your music, using the in-game order: 1 Horizon Pulse, 2 Bass Arena, 3 Horizon XS, 4 Radio Levante, 5 Hospital Records, 6 Ninja Tune and 7 Innovative Leisure. Setting it to 0 takes over every station. Setting `enabled` to 0 turns the mod off and leaves every station vanilla.

The other options are on by default. `mute_dj` silences the DJ and jingles, `show_metadata` puts your song info in the HUD, and `extend_tracks` makes the game change its internal track every 16 minutes instead of every few, which keeps tiny hiccups in your music to a minimum. Changes are picked up while the game is running. Tune away from the station and back to apply them straight away.

The on/off ones (`enabled`, `mute_dj`, `extend_tracks` and `show_metadata`) are also in the game, under Pause > Settings > MODS, and changing them there saves them to `fh2.ini` too. The station is picked in the ini. The radio's own page under MODS > Installed mods has a row that opens the dashboard.

Everything else, like sources, volume and local music folders, is set from the dashboard just like in FH6 Universal Radio.

### Updates

When a newer FH2 Universal Radio is out, the title screen tells you: "PRESS (A) TO START" gets a note like "FH2 Universal Radio 1.9.0 is available" added to it. The update check and the notice come from FH2ML, which does the same for every mod. Whether it checks is up to you, in `FH2ML\config\FH2ML.json`: `"updateChecks": false` turns it off for every mod, and `"mods": {"universalradio": {"updateChecks": false}}` for just the radio. (The old `check=` under `[updates]` in `fh2.ini` isn't read any more.) The version you have is on the MODS tab, in the first line of `bridge.log` and in the DLL's file properties.

## Uninstall

Remove the radio in the FH2ML Manager, or delete `mods\universalradio` from your Azimuth folder. Your settings stay in `FH2ML\data\universalradio`; delete that folder too if you don't want them any more.

## Build from source

The output is the mod, `FH2UniversalRadio.dll`, an FH2ML v4 mod built against FH2ML's API header, which is vendored in `third_party/fh2ml` (MIT). It's built with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases), since the code uses MSVC-style SEH that only Clang supports among the free compilers. On Linux, with CMake and llvm-mingw installed:

```bash
./scripts/get-deps.sh   # one-time: header-only deps
./scripts/build.sh      # compile + stage dist/
```

`dist/` then holds the mod's package, `mods/universalradio/` (`mod.json`, `FH2UniversalRadio.dll`, `fh2-radio-worker.exe`, the `ui` and `assets` folders, `config.example.toml`, `README.txt` and `LICENSE.txt`), ready to zip up: the zip extracts straight into the Azimuth folder, and the FH2ML Manager installs it as it is. GitHub Actions does the same on every push and uploads the result as a build artifact. Pushing a tag like `v1.1.0` also creates a draft release with the zip attached, ready to be published. The version lives in `project(... VERSION x.y.z)` in `CMakeLists.txt`. `mod.json` is generated from `cmake/mod.json.in` with it, and it's the version the DLL tells FH2ML, so the two always match. Bump it before tagging, since the tag build refuses to run if the two don't match, and it's what FH2ML compares against the latest GitHub release. `scripts/install.sh <Azimuth folder>` (or `install.ps1`) copies a build into an Azimuth folder for testing.

FH2ML loads `mods\universalradio\FH2UniversalRadio.dll` (named by `mod.json`), takes the descriptor `FH2ML_GetModAPI_v4` returns, and calls its `Initialize` (both in `src/proxy/dll_main.cpp`) when Azimuth starts. `Initialize` puts the switches on the Mods page, publishes the `FH2UniversalRadio.NowPlaying` interface in [`include/fh2universalradio.h`](include/fh2universalradio.h), which tells other mods what's playing (handy for an overlay or a Discord status mod), and starts the radio's own thread. The mod reads what it ships from `mods\universalradio` and keeps the player's files in `FH2ML\data\universalradio`, the two folders FH2ML hands it. The two call-site patches are FH2ML hooks (`HookCall`): they go in only after Extra Options has finished patching the game, they're listed in FH2ML's crash reports, and other mods can hook the same calls alongside the radio. The log then says `radio hooks installed through FH2ML`. [FH2ML](https://github.com/Refilewski/FH2ML)'s `include/FH2ML_v4.h` and `docs/PORTING-v4.md` document the API for anyone making FH2 mods.

The FH2-specific code is in `src/fh2/` and `include/fh2/`. FH2 links FMOD Studio statically and exports FMOD's low-level API by name. The mod redirects two calls inside the game's `RadioStreamFmod` class, `System::createSound` and `Sound::getSubSound`. For the chosen station, it hands the game a user PCM stream fed from the mod's audio buffer instead of the song from the sound bank, and the game's own Studio event and radio bus take it from there. There's also an offline test in `tests/` (configure with `-DFH2_TESTS=ON`) that maps the real FH2 executable and runs the hooks against a fake FMOD under Wine. `tests/run_hook_test.sh <build dir> <forza_durango_release_final.exe> [RadioInfo_EN.xml]` runs it twice, once with a stand-in for FH2ML's hook services, so the patches are made through `HookCall` as in the game, and once without any services, with the radio patching the calls itself.

## Troubleshooting

Everything gets logged to `FH2ML\data\universalradio\bridge.log` (and what the helper programs say to `helpers.log` next to it), and FH2ML writes `FH2ML\logs\FH2ML.log`, which shows whether the radio was loaded at all. If the game crashes, FH2ML saves a report in `FH2ML\logs\crashes` that says which mod, if any, was involved; attach it when you report a problem. If the mod is working, you'll see `radio hooks installed` shortly after the game starts, and a line ending in `TAKEOVER` whenever the game loads a song on your station.

| Symptom | Fix |
|---|---|
| Dashboard doesn't open | Make sure FH2ML is installed and the radio is in `mods\universalradio` (with its `mod.json` and `ui` folder) and switched on in the FH2ML Manager, then restart Azimuth. `FH2ML\logs\FH2ML.log` should say `universalradio: lifecycle v4 initialized`. |
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
