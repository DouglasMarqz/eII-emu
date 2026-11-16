param([string]$ToolchainRoot=$env:LLVM_MINGW_ROOT,[Parameter(Mandatory)][string]$RomFolder,[Parameter(Mandatory)][string]$Bank)
# Profile-guided Release GUI. Same sources and semantics as build-win64.ps1;
# only code layout/inlining follows a measured training profile. Training runs
# the original firmware on user-supplied ROMs and bank (never stored in Git):
# SSM192k + native DAC + S3528, then Legacy. Run build-win64.ps1 for the tests.
$ErrorActionPreference='Stop'
if (-not $ToolchainRoot) {throw 'Set LLVM_MINGW_ROOT to LLVM-MinGW UCRT x86_64 (tested: 20261006)'}
$compiler=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
$profdata=Join-Path $ToolchainRoot 'bin/llvm-profdata.exe'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    $out='build/win64-pgo'
    New-Item -ItemType Directory -Force $out | Out-Null
    Remove-Item -Force "$out/*.profraw" -ErrorAction SilentlyContinue
    $flags=@('-std=c++20','-O3','-flto','-DNDEBUG','-DUNICODE','-D_UNICODE','-Wall','-Wextra','-pthread','-Isrc','-Ivendor/chips','-static','-DEII_DIAGNOSTICS=0')
    $core=@('src/chips.cpp','src/machine.cpp','vendor/ssm2045-chip/src/chip.cpp','vendor/eiii-cem/eiii_cem3387_v2.cpp','vendor/eiii-cem/eiii_cem_cv_conditioning.cpp')
    & $compiler @flags '-fprofile-instr-generate' @core 'tools/pgo_training.cpp' '-municode' '-o' "$out/pgo_training.exe"
    if ($LASTEXITCODE) {throw 'Training build failed'}
    $runs=@(@('3','1','1'),@('1','0','0'))
    for ($i=0; $i -lt $runs.Count; ++$i) {
        $env:LLVM_PROFILE_FILE="$out/train$i.profraw"
        & "./$out/pgo_training.exe" $RomFolder $Bank @($runs[$i])
        if ($LASTEXITCODE) {throw 'Training run failed'}
    }
    Remove-Item Env:LLVM_PROFILE_FILE
    & $profdata merge -o "$out/eii.profdata" (Get-ChildItem "$out/*.profraw" | ForEach-Object FullName)
    if ($LASTEXITCODE) {throw 'Profile merge failed'}
    $use=@("-fprofile-instr-use=$out/eii.profdata",'-Wno-profile-instr-unprofiled')
    & $compiler @flags @use @core 'src/windows.cpp' '-municode' '-mwindows' '-lgdiplus' '-lcomdlg32' '-lcomctl32' '-luser32' '-lgdi32' '-lole32' '-luuid' '-ladvapi32' '-lwinmm' '-o' "$out/EII-Bringup.exe"
    if ($LASTEXITCODE) {throw 'PGO GUI build failed'}
} finally {Pop-Location}

