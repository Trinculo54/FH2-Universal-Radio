#!/usr/bin/env bash
# Downloads the header-only dependencies into third_party/.
# Run this once after cloning, before configuring CMake.
#
#   $ scripts/get-deps.sh
#
# Re-runs are safe, existing files are overwritten.

set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
tp=$root/third_party

fetch() {
    local url=$1 out=$2 base=${3:-$tp}
    mkdir -p "$(dirname "$base/$out")"
    printf '\033[36m-> %s\033[0m\n' "$out"
    curl -fsSL "$url" -o "$base/$out"
}

fetch https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp nlohmann/nlohmann/json.hpp
fetch https://raw.githubusercontent.com/ToruNiina/toml11/main/single_include/toml.hpp           toml11/toml.hpp
fetch https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h                    miniaudio/miniaudio.h
fetch https://raw.githubusercontent.com/nothings/stb/master/stb_image.h                    stb/stb_image.h
fetch https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h              stb/stb_image_write.h

printf '\n\033[32mAll dependencies fetched into %s.\033[0m\n' "$tp"