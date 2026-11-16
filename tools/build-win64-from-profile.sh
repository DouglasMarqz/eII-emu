#!/usr/bin/env bash
# Matching Windows profiles are collected with capture-win64-pgo.ps1.
set -euo pipefail
if [ "$#" -ne 2 ]; then echo 'usage: build-win64-from-profile.sh LLVM_MINGW_ROOT PROFILE_RESULTS_FOLDER' >&2; exit 2; fi
toolchain=$(cd "$1" && pwd)
profiles=$(cd "$2" && pwd)
cd "$(dirname "$0")/.."
python3 - "$profiles/build-manifest.json" <<'PY'
import json,sys,hashlib,pathlib
m=json.load(open(sys.argv[1]))
for name,expected in m['source_sha256'].items():
    p=pathlib.Path(name)
    if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=expected:
        raise SystemExit('Profile/source mismatch: '+name)
print('Profile source hashes match.')
PY
mkdir -p build/win64-pgo
repo=$(pwd)
out="$repo/build/win64-pgo"
shopt -s nullglob
raw=("$profiles"/*.profraw)
if [ "${#raw[@]}" -eq 0 ]; then echo 'No Windows .profraw files found.' >&2; exit 2; fi
"$toolchain/bin/llvm-profdata" merge "${raw[@]}" -o "$out/eii.profdata"
# The capture used eii-hdd/src/... input names. Preserve those names for
# internal-linkage profile keys even if the checkout has a different name.
work=$(mktemp -d "$out/source-view-XXXXXX")
trap 'rm -f "$work/eii-hdd"; rmdir "$work"' EXIT
ln -s "$repo" "$work/eii-hdd"
cd "$work"
"$toolchain/bin/x86_64-w64-mingw32-clang++" \
 -std=c++20 -O3 -flto -DNDEBUG -DUNICODE -D_UNICODE -DEII_DIAGNOSTICS=0 \
 -pthread -Ieii-hdd/src -Ieii-hdd/vendor/chips -static \
 "-fprofile-instr-use=$out/eii.profdata" \
 -Wno-profile-instr-unprofiled -Werror=profile-instr-out-of-date \
 eii-hdd/src/chips.cpp eii-hdd/src/machine.cpp eii-hdd/vendor/ssm2045-chip/src/chip.cpp eii-hdd/src/windows.cpp \
 -municode -mwindows -lgdiplus -lcomdlg32 -lcomctl32 -luser32 -lgdi32 \
 -lole32 -luuid -ladvapi32 -lwinmm -o "$out/EII-RW-PGO.exe"
sha256sum "$out/EII-RW-PGO.exe"
