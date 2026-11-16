param([string]$ToolchainRoot=$env:LLVM_MINGW_ROOT,[string]$ProfileData,[string]$RomFolder,[string]$Floppy,[switch]$SkipValidator)
# Emulator II VST3 instrument (Windows x64), separate from the standalone.
# Output: build/win64-vst3/EmulatorII-Custom.vst3/Contents/x86_64-win/EmulatorII-Custom.vst3
# -ProfileData <file.profraw|file.profdata>: profile-use build (same counters the
#   standalone PGO release uses; core sources are shared, plug-in code stays
#   unprofiled) into build/win64-vst3-pgo/. The Windows llvm-profdata of
#   20261006 lacks zlib: merge compressed .profraw elsewhere (as
#   build-win64-from-profile.sh does) and pass the merged .profdata.
# -RomFolder/-Floppy: also run the headless host test against caller-supplied
#   ROMs and floppy (never stored in Git).
$ErrorActionPreference='Stop'
if (-not $ToolchainRoot) {throw 'Set LLVM_MINGW_ROOT to LLVM-MinGW UCRT x86_64 (tested: 20261006)'}
$compiler=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
$profdata=Join-Path $ToolchainRoot 'bin/llvm-profdata.exe'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    $out=if ($ProfileData) {'build/win64-vst3-pgo'} else {'build/win64-vst3'}
    $bundle="$out/EmulatorII-Custom.vst3"
    New-Item -ItemType Directory -Force "$bundle/Contents/x86_64-win","$bundle/Contents/Resources" | Out-Null
    $sdk='vendor/vst3sdk'
    $flags=@('-std=c++20','-O3','-flto','-DNDEBUG','-DUNICODE','-D_UNICODE','-Wall','-Wextra','-Wno-pragma-pack','-pthread','-Isrc','-Ivendor/chips',"-I$sdk",'-static','-DEII_DIAGNOSTICS=0','-DEII_VST3_BACKGROUND_AUDIO=1')
    $core=@('src/chips.cpp','src/machine.cpp','vendor/ssm2045-chip/src/chip.cpp')
    $cem=@('vendor/eiii-cem/eiii_cem3387_v2.cpp','vendor/eiii-cem/eiii_cem_cv_conditioning.cpp')
    $ids=@("$sdk/pluginterfaces/base/coreiids.cpp","$sdk/pluginterfaces/base/funknown.cpp","$sdk/public.sdk/source/vst/vstinitiids.cpp")
    $use=@()
    if ($ProfileData) {
        $merged="$out/eii.profdata"
        if ($ProfileData -like '*.profraw') {& $profdata merge -o $merged $ProfileData; if ($LASTEXITCODE) {throw 'Profile merge failed'}}
        else {Copy-Item -Force $ProfileData $merged}
        $use=@("-fprofile-instr-use=$merged",'-Wno-profile-instr-unprofiled','-Werror=profile-instr-out-of-date')
    }
    $libs=@('-lgdiplus','-lcomdlg32','-lcomctl32','-luser32','-lgdi32','-lole32','-luuid','-ladvapi32','-lwinmm')
    & $compiler @flags @use '-shared' @core 'src/vst3/eii_vst3.cpp' 'src/vst3/vst3_iids.cpp' @ids @cem @libs '-Wl,--no-undefined' '-o' "$bundle/Contents/x86_64-win/EmulatorII-Custom.vst3"
    if ($LASTEXITCODE) {throw 'VST3 build failed'}
    Copy-Item -Force "$sdk/LICENSE.txt" "$bundle/Contents/Resources/VST3-SDK-LICENSE.txt"

    & $compiler @flags @core 'tests/vst3_engine_tests.cpp' @cem '-o' "$out/vst3_engine_tests.exe"
    if ($LASTEXITCODE) {throw 'Engine test build failed'}
    & "./$out/vst3_engine_tests.exe"
    if ($LASTEXITCODE) {throw 'Engine tests failed'}

    & $compiler @flags 'vendor/ssm2045-chip/src/chip.cpp' 'tests/legacy_q_tests.cpp' @cem '-o' "$out/legacy_q_tests.exe"
    if ($LASTEXITCODE) {throw 'Legacy Q test build failed'}
    & "./$out/legacy_q_tests.exe"
    if ($LASTEXITCODE) {throw 'Legacy Q tests failed'}

    & $compiler @flags 'vendor/ssm2045-chip/src/chip.cpp' 'tests/sampling_monitor_tests.cpp' @cem '-o' "$out/sampling_monitor_tests.exe"
    if ($LASTEXITCODE) {throw 'Sampling monitor tests compile failed'}
    & "./$out/sampling_monitor_tests.exe"
    if ($LASTEXITCODE) {throw 'Sampling monitor tests failed'}

    & $compiler @flags 'tests/vst3_input_tests.cpp' '-o' "$out/vst3_input_tests.exe"
    if ($LASTEXITCODE) {throw 'Host input tests compile failed'}
    & "./$out/vst3_input_tests.exe"
    if ($LASTEXITCODE) {throw 'Host input tests failed'}

    & $compiler @flags 'tests/filter_service_lifecycle_tests.cpp' '-lcomctl32' '-lcomdlg32' '-luser32' '-lgdi32' '-o' "$out/filter_service_lifecycle_tests.exe"
    if ($LASTEXITCODE) {throw 'Filter Service lifecycle test build failed'}
    & "./$out/filter_service_lifecycle_tests.exe"
    if ($LASTEXITCODE) {throw 'Filter Service lifecycle regression failed'}

    $h="$sdk/public.sdk/source/vst/hosting"
    $hosting=@("$h/module.cpp","$h/module_win32.cpp","$h/hostclasses.cpp","$h/processdata.cpp","$h/eventlist.cpp","$h/parameterchanges.cpp","$h/pluginterfacesupport.cpp",
        "$sdk/public.sdk/source/common/memorystream.cpp","$sdk/public.sdk/source/common/commonstringconvert.cpp","$sdk/public.sdk/source/vst/utility/stringconvert.cpp")
    $hostFlags=@('-std=c++20','-O2','-DNDEBUG','-DUNICODE','-D_UNICODE','-Wno-pragma-pack','-pthread','-Isrc',"-I$sdk",'-static')
    & $compiler @hostFlags 'tools/vst3_host_test.cpp' 'src/vst3/vst3_iids.cpp' @hosting @ids '-lole32' '-luuid' '-lshell32' '-lgdi32' '-luser32' '-lwinmm' '-o' "$out/vst3_host_test.exe"
    if ($LASTEXITCODE) {throw 'Host test build failed'}

    if (-not $SkipValidator) {
        # Steinberg validator, built unmodified from the vendored SDK sources.
        $v="$sdk/public.sdk/samples/vst-hosting/validator"
        $list=Select-String -Path "$v/CMakeLists.txt" -Pattern '\$\{SDK_ROOT\}/(\S+\.cpp)' -AllMatches | ForEach-Object {$_.Matches} | ForEach-Object {"$sdk/"+$_.Groups[1].Value} | Where-Object {$_ -notmatch 'module_(mac|linux)'}
        $list+=@("$v/source/main.cpp","$v/source/usediids.cpp","$v/source/validator.cpp")
        $extra=@("$h/module.cpp","$h/hostclasses.cpp","$h/processdata.cpp","$h/eventlist.cpp","$h/parameterchanges.cpp","$h/pluginterfacesupport.cpp","$h/connectionproxy.cpp",
            "$sdk/public.sdk/source/common/commonstringconvert.cpp","$sdk/public.sdk/source/common/commoniids.cpp","$sdk/public.sdk/source/common/threadchecker_win32.cpp",
            "$sdk/public.sdk/source/vst/vstpresetfile.cpp","$sdk/public.sdk/source/vst/utility/stringconvert.cpp","$sdk/pluginterfaces/base/ustring.cpp","$sdk/pluginterfaces/base/conststringtable.cpp")
        $base=@(Get-ChildItem "$sdk/base/source/*.cpp","$sdk/base/thread/source/*.cpp" | ForEach-Object {$_.FullName})
        & $compiler '-std=c++17' '-O1' '-DNDEBUG' '-DUNICODE' '-D_UNICODE' '-w' '-municode' "-I$sdk" '-static' @list @extra @base @ids '-lole32' '-luuid' '-lshell32' '-o' "$out/validator.exe"
        if ($LASTEXITCODE) {throw 'Validator build failed'}
        & "./$out/validator.exe" "$bundle" | Tee-Object -FilePath "$out/validator.log" | Select-Object -Last 8
        if ($LASTEXITCODE) {throw 'Steinberg validator reported failures'}
    }
    if ($RomFolder -and $Floppy) {
        & "./$out/vst3_host_test.exe" "$bundle" $RomFolder $Floppy (Resolve-Path $out).Path | Tee-Object -FilePath "$out/host_test.log"
        if ($LASTEXITCODE) {throw 'VST3 host test failed'}
    }
} finally {Pop-Location}
