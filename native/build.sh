#!/usr/bin/env bash
# Builds the native Linux helper (static-PIE, x86_64, no runtime deps) with zig cc,
# plus the /bin/sh wrapper that Wine actually launches.
# Why both: Proton's Wine installs a seccomp filter that traps (SIGSYS) every
# syscall made from code mapped below 4 GB, and it is inherited across exec().
# A static non-PIE binary (loaded at 0x1000000) dies instantly with SIGSYS.
# A static-PIE is loaded high by the kernel, but Wine's CreateProcess treats an
# ELF of type ET_DYN without PT_INTERP as a builtin library, so it can't be
# launched directly: point [native].helper at the wrapper script instead.
# The same rule applies to ffmpeg: use a PIE/dynamic build (e.g. BtbN), not a
# static non-PIE one (johnvansickle builds die with SIGSYS under Proton).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/../build-native}
mkdir -p "$out"
zig cc -target x86_64-linux-musl -static-pie -fPIE -O2 -s -Wall -Wextra \
    -o "$out/fh2-radio-native-helper" "$here/fh2_native_helper.c"
cat > "$out/fh2-radio-native-helper.sh" <<'SH'
#!/bin/sh
# Wine/Proton launches this script; it execs the static-PIE helper next to it.
exec "$(dirname "$0")/fh2-radio-native-helper" "$@"
SH
chmod +x "$out/fh2-radio-native-helper.sh"
file "$out/fh2-radio-native-helper" 2>/dev/null || true
