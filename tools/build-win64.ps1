param([string]$ToolchainRoot=$env:LLVM_MINGW_ROOT,[switch]$Diagnostics,[switch]$TimingLab)
$ErrorActionPreference='Stop'
if (-not $ToolchainRoot) {throw 'Set LLVM_MINGW_ROOT to LLVM-MinGW UCRT x86_64 (tested: 20261006)'}
$compiler=Join-Path $ToolchainRoot 'bin/x86_64-w64-mingw32-clang++.exe'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force 'build/win64' | Out-Null
    $flags=@('-std=c++20','-O3','-flto','-DNDEBUG','-DUNICODE','-D_UNICODE','-Wall','-Wextra','-pthread','-Isrc','-Ivendor/chips','-static')
    $flags+=('-DEII_DIAGNOSTICS='+[int]$Diagnostics.IsPresent)
    $core=@('src/chips.cpp','src/machine.cpp','vendor/ssm2045-chip/src/chip.cpp','vendor/eiii-cem/eiii_cem3387_v2.cpp','vendor/eiii-cem/eiii_cem_cv_conditioning.cpp')
    & $compiler @flags @core 'src/windows.cpp' '-municode' '-mwindows' '-lgdiplus' '-lcomdlg32' '-lcomctl32' '-luser32' '-lgdi32' '-lole32' '-luuid' '-ladvapi32' '-lwinmm' '-o' 'build/win64/EII-Bringup.exe'
    if ($LASTEXITCODE) {throw 'GUI build failed'}
    & $compiler @flags 'tests/vector_panel_tests.cpp' '-o' 'build/win64/vector_panel_tests.exe'
    if ($LASTEXITCODE) {throw 'Vector panel test build failed'}
    & './build/win64/vector_panel_tests.exe'
    if ($LASTEXITCODE) {throw 'Vector panel tests failed'}
    $spanFlags=@($flags | Where-Object {$_ -notlike '-DEII_DIAGNOSTICS=*'}) + @('-DEII_DIAGNOSTICS=1')
    & $compiler @spanFlags 'tests/production_span_tests.cpp' '-o' 'build/win64/production_span_tests.exe'
    if ($LASTEXITCODE) {throw 'Production span oracle build failed'}
    & './build/win64/production_span_tests.exe'
    if ($LASTEXITCODE) {throw 'Production span oracle failed'}
    & $compiler @flags @core 'tests/production_span_runtime_tests.cpp' '-o' 'build/win64/production_span_runtime_tests.exe'
    if ($LASTEXITCODE) {throw 'Production span runtime build failed'}
    & './build/win64/production_span_runtime_tests.exe'
    if ($LASTEXITCODE) {throw 'Production span runtime failed'}
    & $compiler @flags @core 'tools/boot_probe.cpp' '-o' 'build/win64/eii_probe.exe'
    if ($LASTEXITCODE) {throw 'Probe build failed'}
    & $compiler @flags @core 'tools/front_panel_probe.cpp' '-o' 'build/win64/front_panel_probe.exe'
    if ($LASTEXITCODE) {throw 'Panel probe build failed'}
    foreach ($hddTest in @('hard_disk','hdd_image_file','floppy_write','floppy_write_event')) {
        & $compiler @flags "tests/${hddTest}_tests.cpp" '-o' "build/win64/${hddTest}_tests.exe"
        if ($LASTEXITCODE) {throw 'HDD test build failed'}
        & "./build/win64/${hddTest}_tests.exe"
        if ($LASTEXITCODE) {throw 'HDD tests failed'}
    }
    & $compiler @flags @core 'tools/hdd_panel_probe.cpp' '-o' 'build/win64/hdd_panel_probe.exe'
    if ($LASTEXITCODE) {throw 'HDD panel probe build failed'}
    & $compiler @flags @core 'tests/floppy_sio_wiring_tests.cpp' '-o' 'build/win64/floppy_sio_wiring_tests.exe'
    if ($LASTEXITCODE) {throw 'Floppy wiring test build failed'}
    & './build/win64/floppy_sio_wiring_tests.exe'
    if ($LASTEXITCODE) {throw 'Floppy wiring test failed'}
    & $compiler @flags @core 'tools/floppy_rw_session_probe.cpp' '-o' 'build/win64/floppy_rw_session_probe.exe'
    if ($LASTEXITCODE) {throw 'Floppy R/W session probe build failed'}
    if ($TimingLab) {
        $labFlags=@($flags | Where-Object {$_ -notlike '-DEII_DIAGNOSTICS=*'}) + @('-DEII_DIAGNOSTICS=0','-DEII_PLAYBACK_TIMING_LAB=1')
        & $compiler @labFlags @core 'tests/playback_timing_observer_tests.cpp' '-o' 'build/win64/playback_timing_observer_tests.exe'
        if ($LASTEXITCODE) {throw 'Timing observer build failed'}
        & './build/win64/playback_timing_observer_tests.exe'
        if ($LASTEXITCODE) {throw 'Timing observer tests failed'}
        & $compiler @labFlags @core 'tools/playback_fidelity_probe.cpp' '-o' 'build/win64/playback_fidelity_probe.exe'
        if ($LASTEXITCODE) {throw 'Timing probe build failed'}
        & $compiler @labFlags @core 'tests/native_timing_tests.cpp' '-o' 'build/win64/native_timing_tests.exe'
        if ($LASTEXITCODE) {throw 'Native timing test build failed'}
        & './build/win64/native_timing_tests.exe'
        if ($LASTEXITCODE) {throw 'Native timing tests failed'}
        & $compiler @labFlags @core 'tools/native_audio_probe.cpp' '-o' 'build/win64/native_audio_probe.exe'
        if ($LASTEXITCODE) {throw 'Native audio probe build failed'}
    }
    foreach ($traceTest in @('performance_trace','performance_trace_session','s3528_runtime','s3528_voice','s3528_pcm','native_audio','native_pcm')) {
        & $compiler @flags @core "tests/${traceTest}_tests.cpp" '-o' "build/win64/${traceTest}_tests.exe"
        if ($LASTEXITCODE) {throw 'Performance trace test build failed'}
        & "./build/win64/${traceTest}_tests.exe"
        if ($LASTEXITCODE) {throw 'Performance trace tests failed'}
    }
    & $compiler @flags 'tests/panel_report_text_tests.cpp' '-o' 'build/win64/panel_report_text_tests.exe'
    if ($LASTEXITCODE) {throw 'Report policy build failed'}
    & './build/win64/panel_report_text_tests.exe'
    if ($LASTEXITCODE) {throw 'Report policy tests failed'}
    & $compiler @flags @core 'tests/live_report_session_tests.cpp' '-o' 'build/win64/live_report_session_tests.exe'
    if ($LASTEXITCODE) {throw 'Live report worker build failed'}
    & './build/win64/live_report_session_tests.exe'
    if ($LASTEXITCODE) {throw 'Live report worker tests failed'}
    & $compiler @flags @core 'tests/front_panel_session_tests.cpp' '-o' 'build/win64/front_panel_session_tests.exe'
    if ($LASTEXITCODE) {throw 'Panel test build failed'}
    & './build/win64/front_panel_session_tests.exe'
    if ($LASTEXITCODE) {throw 'Panel tests failed'}
    & $compiler @flags @core 'tests/sample_transport_tests.cpp' '-o' 'build/win64/sample_transport_tests.exe'
    if ($LASTEXITCODE) {throw 'Sample transport test build failed'}
    & './build/win64/sample_transport_tests.exe'
    if ($LASTEXITCODE) {throw 'Sample transport tests failed'}
    & $compiler @flags 'vendor/ssm2045-chip/src/chip.cpp' 'vendor/eiii-cem/eiii_cem3387_v2.cpp' 'vendor/eiii-cem/eiii_cem_cv_conditioning.cpp' 'tests/runtime_audio_tests.cpp' '-o' 'build/win64/runtime_audio_tests.exe'
    if ($LASTEXITCODE) {throw 'Audio transport test build failed'}
    & './build/win64/runtime_audio_tests.exe'
    if ($LASTEXITCODE) {throw 'Audio transport tests failed'}
    & $compiler @flags 'tests/sample_input_live_tests.cpp' '-o' 'build/win64/sample_input_live_tests.exe'
    if ($LASTEXITCODE) {throw 'Sample input jitter buffer test build failed'}
    & './build/win64/sample_input_live_tests.exe'
    if ($LASTEXITCODE) {throw 'Sample input jitter buffer tests failed'}
    & $compiler @flags 'vendor/ssm2045-chip/src/chip.cpp' 'vendor/eiii-cem/eiii_cem3387_v2.cpp' 'vendor/eiii-cem/eiii_cem_cv_conditioning.cpp' 'tests/chip_audio_tests.cpp' '-o' 'build/win64/chip_audio_tests.exe'
    if ($LASTEXITCODE) {throw 'Chip audio test build failed'}
    & './build/win64/chip_audio_tests.exe'
    if ($LASTEXITCODE) {throw 'Chip audio tests failed'}
    & $compiler @flags @core 'tests/diagnostics_tests.cpp'  '-o' 'build/win64/diagnostics_tests.exe'
    if ($LASTEXITCODE) {throw 'Diagnostics test build failed'}
    & './build/win64/diagnostics_tests.exe'
    if ($LASTEXITCODE) {throw 'Diagnostics tests failed'}
    & $compiler @flags @core 'tests/midi_serial_tests.cpp' '-o' 'build/win64/midi_serial_tests.exe'
    if ($LASTEXITCODE) {throw 'MIDI test build failed'}
    & './build/win64/midi_serial_tests.exe'
    if ($LASTEXITCODE) {throw 'MIDI tests failed'}
    & $compiler @flags @core 'tests/digital_tests.cpp' '-o' 'build/win64/digital_tests.exe'
    if ($LASTEXITCODE) {throw 'Test build failed'}
    & './build/win64/digital_tests.exe'
    if ($LASTEXITCODE) {throw 'Digital tests failed'}
} finally {Pop-Location}


