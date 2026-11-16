#!/usr/bin/env bash
set -euo pipefail
cd /workspace/scratch/22ef99685be2
# Separate offline ETL reader; NEVER rebuilds/relinks the plugin.
tmp/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64/bin/x86_64-w64-mingw32-clang++ -std=c++20 -O3 -flto -DNDEBUG -DUNICODE -D_UNICODE -DEII_DIAGNOSTICS=0 -DEII_VST3_BACKGROUND_AUDIO=1 -Wno-pragma-pack -pthread -static -fprofile-instr-use=/workspace/scratch/22ef99685be2/eii-hdd/build/win64-pgo/eii.profdata -Wno-profile-instr-unprofiled -Werror=profile-instr-out-of-date -municode profile037/etl_samples.cpp -ladvapi32 -ltdh -Wl,--no-undefined -o profile037/etl_samples.exe
