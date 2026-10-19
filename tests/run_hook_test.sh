#!/usr/bin/env bash
# Runs fh2_hook_test under Wine twice: with a stand-in for FH2ML's services, so
# the call sites are patched through HookCall/EnableHooks as in the game, and
# without any services, so the radio patches the call sites itself.
#   tests/run_hook_test.sh <build dir> <forza_durango_release_final.exe> [RadioInfo_EN.xml]
set -uo pipefail
b=$(cd "$1" && pwd)
win_exe=$(winepath -w "$2")
xml=()
[ -n "${3:-}" ] && xml=("$(winepath -w "$3")")
status=0
echo "==== through FH2ML's services"
FH2_TEST_FH2ML=1 wine "$b/fh2_hook_test.exe" "$win_exe" "${xml[@]}" || status=1
echo "==== on its own"
wine "$b/fh2_hook_test.exe" "$win_exe" "${xml[@]}" || status=1
exit $status
