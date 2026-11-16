@echo off
rem Local strict-PGO training: headless EII-PGO-TRAIN, four analog models. Name
rem compression is disabled so the llvm-mingw llvm-profdata can merge on Windows
rem (no Linux step). Then: build-win64-vst3.ps1 -ProfileData <out>\eii.profdata
rem usage: pgo-train-win64.cmd ROM_FOLDER BANK_IMAGE OUT_FOLDER
set T=%LLVM_MINGW_ROOT%\bin
set O=%~3
set L=%O%\train.log
if not exist %O% mkdir %O%
cd /d %~dp0..
for /f %%h in ('git rev-parse --short HEAD') do set COMMIT=%%h
echo commit %COMMIT% > %L%
%T%\x86_64-w64-mingw32-clang++.exe -std=c++20 -O3 -flto -DNDEBUG -DUNICODE -D_UNICODE -DEII_DIAGNOSTICS=0 -DEII_VST3_BACKGROUND_AUDIO=1 -Wno-pragma-pack -pthread -Isrc -Ivendor/chips -Ivendor/vst3sdk -static -fprofile-instr-generate -mllvm -enable-name-compression=false src/chips.cpp src/machine.cpp vendor/ssm2045-chip/src/chip.cpp vendor/eiii-cem/eiii_cem3387_v2.cpp vendor/eiii-cem/eiii_cem_cv_conditioning.cpp tools/custom_eiii_pgo_training.cpp -municode -o %O%\EII-PGO-TRAIN.exe >> %L% 2>&1
echo TRAIN_BUILD=%ERRORLEVEL% >> %L%
for %%M in (ssm192 legacy eiii model2) do (
  set LLVM_PROFILE_FILE=%O%\%%M.profraw
  %O%\EII-PGO-TRAIN.exe %~1 "%~2" %%M > %O%\%%M.log 2>&1
  echo %%M rc=%ERRORLEVEL% >> %L%
)
set LLVM_PROFILE_FILE=
%T%\llvm-profdata.exe merge -o %O%\eii.profdata %O%\ssm192.profraw %O%\legacy.profraw %O%\eiii.profraw %O%\model2.profraw >> %L% 2>&1
echo MERGE=%ERRORLEVEL% >> %L%
echo DONE >> %L%
