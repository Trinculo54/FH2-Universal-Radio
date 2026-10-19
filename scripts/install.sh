#!/usr/bin/env bash
# Copies the built mod into an Azimuth folder (the one with azimuth.exe), as
# mods/universalradio, where FH2ML loads it. Run after build.sh.
#
#   $ scripts/install.sh /path/to/Azimuth
#
# The mod's folder is replaced, the way an update through the FH2ML Manager
# replaces it. Your fh2.ini, config.toml, Spotify login and downloaded helpers
# live in FH2ML/data/universalradio and aren't touched.

set -euo pipefail

azimuth=${1:?Usage: $0 <azimuth-dir>}
root=$(cd "$(dirname "$0")/.." && pwd)
pkg=$root/dist/mods/universalradio

[ -f "$pkg/mod.json" ] || { echo "dist/mods/universalradio not found, run scripts/build.sh first." >&2; exit 1; }
[ -d "$azimuth" ] || { echo "Azimuth directory not found: $azimuth" >&2; exit 1; }
[ -f "$azimuth/azimuth.exe" ] || {
    printf '\033[33mazimuth.exe not found in %s, make sure this is the right folder.\033[0m\n' "$azimuth" >&2
    exit 1
}
[ -f "$azimuth/data/bin/FH2ML.Runtime.dll" ] ||
    printf "\033[33mFH2ML isn't installed in %s yet; the radio needs it (https://github.com/Refilewski/FH2ML).\033[0m\n" "$azimuth" >&2

dst=$azimuth/mods/universalradio
rm -rf "$dst"
mkdir -p "$azimuth/mods"
cp -r "$pkg" "$dst"
(cd "$dst" && find . -type f -printf '  + mods/universalradio/%P\n')

printf '\n\033[32mDone. Launch FH2, set Audio > Radio DJ = Off and tune to Horizon Pulse.\033[0m\n'
echo "Then open http://localhost:8420 in any browser on this machine."
