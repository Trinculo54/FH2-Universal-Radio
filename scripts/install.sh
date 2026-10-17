#!/usr/bin/env bash
# Copies the built mod into an Azimuth folder (the one with azimuth.exe).
# Run after build.sh.
#
#   $ scripts/install.sh /path/to/Azimuth
#
# Existing files are backed up to *.bak before being overwritten. Your
# config.toml, fh2.ini, Spotify login and downloaded helpers are kept.

set -euo pipefail

azimuth=${1:?Usage: $0 <azimuth-dir>}
root=$(cd "$(dirname "$0")/.." && pwd)
dist=$root/dist

[ -f "$dist/d3dcompiler_47.dll" ] || { echo "dist/d3dcompiler_47.dll not found, run scripts/build.sh first." >&2; exit 1; }
[ -d "$azimuth" ] || { echo "Azimuth directory not found: $azimuth" >&2; exit 1; }
[ -f "$azimuth/azimuth.exe" ] || {
    printf '\033[33mazimuth.exe not found in %s, make sure this is the right folder.\033[0m\n' "$azimuth" >&2
    exit 1
}

# Copy $1 to $2, backing up any existing target to *.bak.
backup_and_copy() {
    local src=$1 dst=$2
    mkdir -p "$(dirname "$dst")"
    [ -f "$dst" ] && cp -f "$dst" "$dst.bak"
    cp -f "$src" "$dst"
    printf '  + %s\n' "${dst#"$azimuth/"}"
}

backup_and_copy "$dist/d3dcompiler_47.dll" "$azimuth/d3dcompiler_47.dll"

mkdir -p "$azimuth/fh2-radio"
cp -rfT "$dist/fh2-radio/ui" "$azimuth/fh2-radio/ui"
cp -rfT "$dist/fh2-radio/assets" "$azimuth/fh2-radio/assets"
cp -f "$dist/README.txt" "$azimuth/fh2-radio/README.txt"
if [ ! -f "$azimuth/fh2-radio/config.toml" ]; then
    cp "$dist/fh2-radio/config.toml" "$azimuth/fh2-radio/config.toml"
    printf '\033[33m  + fh2-radio/config.toml  (seeded from example)\033[0m\n'
fi
backup_and_copy "$dist/fh2-radio/fh2-radio-worker.exe" "$azimuth/fh2-radio/fh2-radio-worker.exe"

printf '\n\033[32mDone. Launch FH2, set Audio > Radio DJ = Off and tune to Horizon Pulse.\033[0m\n'
echo "Then open http://localhost:8420 in any browser on this machine."
