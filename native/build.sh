#!/usr/bin/env bash
# Builds the native Linux helper (static, x86_64, no runtime deps) with zig cc.
# Must NOT be a static-PIE: Wine's CreateProcess treats an ELF of type ET_DYN
# without PT_INTERP as a Wine builtin library instead of exec()ing it.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/../build-native}
mkdir -p "$out"
zig cc -target x86_64-linux-musl -static -fno-pie -no-pie -O2 -s -Wall -Wextra \
    -o "$out/fh2-radio-native-helper" "$here/fh2_native_helper.c"
file "$out/fh2-radio-native-helper" 2>/dev/null || true
