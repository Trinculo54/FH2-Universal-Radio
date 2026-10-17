#!/usr/bin/env bash
# Runs fh2_hook_test under Wine twice: on its own (the radio patches the call
# sites itself, as with platform 2.1 and older) and as azimuth.exe next to
# the real FH2 Mod Platform, so the patches are platform hooks.
#   tests/run_platform_hook_test.sh <build dir> <forza_durango_release_final.exe> [RadioInfo_EN.xml]
set -euo pipefail
b=$(cd "$1" && pwd)
exe=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
xml=()
[ -n "${3:-}" ] && xml=("$(winepath -w "$3")")
win_exe=$(winepath -w "$exe")
echo "==== on its own"
wine "$b/fh2_hook_test.exe" "$win_exe" "${xml[@]}"
echo "==== through the platform"
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir -p "$dir/fh2-mods" "$dir/FH2/Mount/media/Stripped/StringTables"
cp "$b/fh2_hook_test.exe" "$dir/azimuth.exe"
cp "$b/platform/d3dcompiler_47.dll" "$dir/"
cp "$b/RadioShim.dll" "$dir/fh2-mods/"
ln -s "$exe" "$dir/FH2/Mount/forza_durango_release_final.exe"
(cd "$dir" && FH2_TEST_PLATFORM=1 wine azimuth.exe "$win_exe" "${xml[@]}")
status=$?
echo "---- platform.log"
cat "$dir/fh2-mods/platform.log"
exit $status
