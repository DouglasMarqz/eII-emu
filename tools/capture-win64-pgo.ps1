# Run the instrumented GUI, then package only compiler counters and build IDs.
# No ROM, disk, RAM dump, audio or registry export is included in the result ZIP.
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$program=Join-Path $root 'EII-PROFIL-MERES.exe'
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$output=Join-Path $root ('pgo-'+$stamp)
$oldProfile=$env:LLVM_PROFILE_FILE
try {
    if (-not (Test-Path -LiteralPath $program)) {throw 'EII-PROFIL-MERES.exe is missing; extract the full ZIP first.'}
    New-Item -ItemType Directory -Path $output | Out-Null
    $env:LLVM_PROFILE_FILE=Join-Path $output 'eii-%p.profraw'
    Write-Host 'Profilmeres: bootolj, jatssz, nyomj nehany panelgombot, majd zard be szabalyosan az EII ablakot.'
    Write-Host 'Ez meroprogram; a vegleges sebesseget nem ezen merjuk.'
    $process=Start-Process -FilePath $program -WorkingDirectory $root -Wait -PassThru
    if ($process.ExitCode -ne 0) {throw ('The profiling program exited with code '+$process.ExitCode+'. Keep the pgo folder for diagnosis.')}
    $profiles=@(Get-ChildItem -LiteralPath $output -Filter '*.profraw' | Where-Object Length -gt 0)
    if (-not $profiles.Count) {throw 'No compiler profile was written. Close the EII window normally; do not terminate it in Task Manager.'}
    $manifest=Join-Path $root 'build-manifest.json'
    if (Test-Path -LiteralPath $manifest) {Copy-Item -LiteralPath $manifest -Destination $output}
    $archive=Join-Path $root ('PGO-EREDMENY-'+$stamp+'.zip')
    $files=@(Get-ChildItem -LiteralPath $output -File | ForEach-Object FullName)
    Compress-Archive -LiteralPath $files -DestinationPath $archive
    Write-Host ('KESZ: '+$archive)
} finally {
    if ($null -eq $oldProfile) {Remove-Item Env:LLVM_PROFILE_FILE -ErrorAction SilentlyContinue}
    else {$env:LLVM_PROFILE_FILE=$oldProfile}
}
