#!/usr/bin/env bash
# Linux read-only workload; private assets are supplied by the caller.
set -euo pipefail
if [[ $# -lt 2 || $# -gt 4 ]]; then
  echo 'usage: profile-control.sh ROM-folder bank-image [gprof|release] [spans|edges|sampling]' >&2
  exit 2
fi
profile_rom=$(realpath "$1")
profile_bank=$(realpath "$2")
profile_mode=${3:-release}
profile_workload=${4:-spans}
cd "$(dirname "$0")/.."
profile_flags=()
if [[ $profile_mode == gprof ]]; then
  profile_flags=(-pg -DEII_GPROF=1)
elif [[ $profile_mode != release ]]; then
  echo 'expected gprof or release' >&2
  exit 2
fi
profile_build="build/control-profile-$profile_mode"
mkdir -p "$profile_build"
"${CXX:-g++}" -std=c++20 -O3 -DEII_DIAGNOSTICS=0 -pthread "${profile_flags[@]}" \
  -Isrc -Ivendor/chips tools/control_profile_probe.cpp src/machine.cpp src/chips.cpp \
  vendor/ssm2045-chip/src/chip.cpp -o "$profile_build/probe"
(
  cd "$profile_build"
  ./probe "$profile_rom" "$profile_bank" "$profile_workload" | tee workload.txt
  if [[ $profile_mode == gprof ]]; then
    gprof -b ./probe gmon.out > gprof.txt
  fi
)
