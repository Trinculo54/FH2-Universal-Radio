#!/usr/bin/env bash
# Cross-compiles FH2 Universal Radio, an FH2ML v4 mod, from Linux via
# llvm-mingw, then stages the mod's package in dist/.
#
#   $ scripts/build.sh
#
# Output mirrors scripts/build.ps1. dist/ holds mods/universalradio/, so a zip
# of it extracts straight into the Azimuth folder (or goes to the FH2ML
# Manager's Install mod):
#   mod.json                FH2ML's manifest, with the build's version
#   FH2UniversalRadio.dll   the radio
#   fh2-radio-worker.exe    starts the helper programs outside the game
#   ui/                     dashboard (served at http://localhost:8420)
#   assets/                 default artwork
#   config.example.toml     copied to FH2ML/data/universalradio/config.toml
#                           on the first launch
#   README.txt, LICENSE.txt
#
# Env vars:
#   PATH            must contain x86_64-w64-mingw32-clang++; if your llvm-mingw
#                   lives at /opt/llvm-mingw, this script adds it automatically
#   JOBS            -j parallelism (default: nproc)

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/build
dist=$root/dist

# llvm-mingw is the Clang-based mingw-w64 toolchain. The codebase relies on
# MSVC SEH (__try / __except), which only Clang implements among the free
# cross-compilers. The CMake toolchain file probes the same locations.
for d in /opt/llvm-mingw/bin /usr/local/llvm-mingw/bin; do
    [ -d "$d" ] && PATH=$d:$PATH
done

if ! command -v x86_64-w64-mingw32-clang++ >/dev/null; then
    cat >&2 <<'EOF'
x86_64-w64-mingw32-clang++ not found. Install llvm-mingw:
  Arch     : sudo pacman -S llvm-mingw
  Manual   : https://github.com/mstorsjo/llvm-mingw/releases
EOF
    exit 1
fi

if [ ! -f "$root/third_party/nlohmann/nlohmann/json.hpp" ]; then
    printf '\033[33mthird_party/ has no downloaded headers yet, running get-deps.sh first.\033[0m\n'
    "$root/scripts/get-deps.sh"
fi

printf '\033[36m-> cmake configure\033[0m\n'
cmake -S "$root" -B "$build" \
      -DCMAKE_TOOLCHAIN_FILE="$root/cmake/mingw-w64-x86_64.cmake" \
      -DCMAKE_BUILD_TYPE=Release

printf '\033[36m-> cmake build (Release)\033[0m\n'
cmake --build "$build" -j "${JOBS:-$(nproc)}"

# Validate assets are present before copying
if [ ! -f "$root/assets/default_artwork.png" ]; then
    echo "Missing required assets (e.g., default_artwork.png). Please run scripts/get-deps.sh again to fetch missing dependencies." >&2
    exit 1
fi

pkg=$dist/mods/universalradio
rm -rf "$dist"
mkdir -p "$pkg/assets"

cp "$build/mod.json"                  "$pkg/"
cp "$build/FH2UniversalRadio.dll"     "$pkg/"
cp "$build/fh2-radio-worker.exe"      "$pkg/"
cp -r "$root/ui/dist"                 "$pkg/ui"
cp "$root/assets/default_artwork.png" "$pkg/assets/default_artwork.png"
cp "$root/config.example.toml"        "$pkg/"
cp "$root/scripts/dist-readme.txt"    "$pkg/README.txt"
cp "$root/LICENSE"                    "$pkg/LICENSE.txt"

printf '\n\033[32mBuilt + staged in %s\033[0m\n' "$dist"
(cd "$dist" && find . -type f -printf '  %P\n')
