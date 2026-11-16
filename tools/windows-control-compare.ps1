param([string]$RomFolder='', [string]$Bank='')
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
Add-Type -AssemblyName System.Windows.Forms
if (-not $RomFolder) {
  $pick=New-Object System.Windows.Forms.FolderBrowserDialog
  $pick.Description='Select the EII ROM folder (main.bin, scanner.bin, ic135.bin, ic137.bin)'
  if ($pick.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return }
  $RomFolder=$pick.SelectedPath
}
if (-not $Bank) {
  $pick=New-Object System.Windows.Forms.OpenFileDialog
  $pick.Title='Select a bootable EII floppy bank (HFE or 573440-byte EMUIIFD)'
  $pick.Filter='EII bank|*.hfe;*.emuiifd;*.img;*.bin|All files|*.*'
  if ($pick.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return }
  $Bank=$pick.FileName
}
$workName='work-'+[Guid]::NewGuid().ToString('N')
$work=Join-Path $root $workName
$resultName='results-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,6)
$result=Join-Path $root $resultName
New-Item -ItemType Directory -Path (Join-Path $work 'roms') -Force | Out-Null
New-Item -ItemType Directory -Path $result | Out-Null
Push-Location $root
try {
  foreach ($name in @('main.bin','scanner.bin','ic135.bin','ic137.bin')) {
    $source=Join-Path $RomFolder $name
    if (($name -eq 'main.bin') -and -not (Test-Path -LiteralPath $source)) { $source=Join-Path $RomFolder 'main23.bin' }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing ROM: $name" }
    Copy-Item -LiteralPath $source -Destination (Join-Path (Join-Path $work 'roms') $name)
  }
  Copy-Item -LiteralPath $Bank -Destination (Join-Path $work 'bank.img')
  $runs=@(@{Tag='baseline-1';Exe='baseline'},@{Tag='candidate-1';Exe='candidate'},@{Tag='candidate-2';Exe='candidate'},@{Tag='baseline-2';Exe='baseline'})
  $summary=@()
  foreach ($run in $runs) {
    Write-Host ('Running '+$run.Tag+' (real firmware boot, notes, sampling)...')
    $output=@(& (Join-Path $root ('EII-control-'+$run.Exe+'.exe')) ($workName+'\roms') ($workName+'\bank.img') 'sampling' 2>&1)
    $code=$LASTEXITCODE
    $lines=@($output | ForEach-Object { $_.ToString() })
    $lines | Set-Content -LiteralPath (Join-Path $result ($run.Tag+'.txt')) -Encoding UTF8
    if ($code -ne 0) { throw ('Benchmark failed: '+$run.Tag+'; see its text log') }
    if (-not ($lines | Where-Object { $_ -match '^LCD \[P\d{2}' })) { throw 'Bank did not reach a loaded preset; choose a bootable floppy bank' }
    $seconds=0.0
    foreach ($line in $lines) {
      if ($line -match '^(\S+) ticks=\d+ wall=([0-9.eE+-]+)') {
        if ($Matches[1] -ne 'boot') { $seconds += [double]::Parse($Matches[2],[Globalization.CultureInfo]::InvariantCulture) }
      }
    }
    $audio=@($lines | Where-Object { $_ -match '^AUDIO_HASH ' })
    $state=@($lines | Where-Object { $_ -match '^END memory=' })
    $sampling=@($lines | Where-Object { $_ -match '^SAMPLING ' })
    if ($audio.Count -ne 1 -or $state.Count -ne 1 -or $sampling.Count -ne 1 -or $seconds -le 0) { throw 'Incomplete benchmark output' }
    $samplingGood=($sampling[0] -match 'reads=[1-9][0-9]* unknown=0 .*LCD=\[Sample is good')
    $summary += [pscustomobject]@{Run=$run.Tag;PostBootSeconds=$seconds;AudioHash=$audio[0];State=$state[0];SamplingGood=$samplingGood;Sampling=$sampling[0]}
    Write-Host ('  post-boot '+$seconds.ToString('F4',[Globalization.CultureInfo]::InvariantCulture)+' seconds')
  }
  $sameAudio=@($summary.AudioHash | Select-Object -Unique).Count -eq 1
  $sameState=@($summary.State | Select-Object -Unique).Count -eq 1
  $allSampling=@($summary | Where-Object { -not $_.SamplingGood }).Count -eq 0
  $baseMean=($summary | Where-Object { $_.Run -like 'baseline-*' } | Measure-Object PostBootSeconds -Average).Average
  $candidateMean=($summary | Where-Object { $_.Run -like 'candidate-*' } | Measure-Object PostBootSeconds -Average).Average
  $cpu='unknown'
  try { $cpu=(Get-ItemProperty 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0').ProcessorNameString } catch {}
  [pscustomobject]@{Version='control-compare-1';Cpu=$cpu;OS=[Environment]::OSVersion.VersionString;AudioHashesMatch=$sameAudio;StateHashesMatch=$sameState;AllSamplingGood=$allSampling;BaselineMeanSeconds=$baseMean;CandidateMeanSeconds=$candidateMean;RuntimeReductionPercent=100*(1-$candidateMean/$baseMean);Runs=$summary} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $result 'summary.json') -Encoding UTF8
  if (-not ($sameAudio -and $sameState -and $allSampling)) { Write-Warning 'Equivalence or sampling check failed. Do not use the candidate in a plugin.' }
  Write-Host ('Audio/state hashes match: '+$sameAudio+'/'+$sameState+'; all sampling good: '+$allSampling)
  Write-Host ('Runtime reduction (positive is faster): '+(100*(1-$candidateMean/$baseMean)).ToString('F2')+'%')
} catch {
  'Comparison did not complete. Check the local console for input or Windows execution errors.' | Set-Content -LiteralPath (Join-Path $result 'error.txt') -Encoding UTF8
  Write-Host $_.Exception.Message -ForegroundColor Red
} finally {
  Pop-Location
  # Only the unique temporary work folder created above is removed. Results
  # contain numeric logs, never ROM or bank payloads or source paths.
  if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
  $resultZip=Join-Path $root ($resultName+'.zip')
  Compress-Archive -Path (Join-Path $result '*') -DestinationPath $resultZip
  Write-Host ('Please return: '+$resultZip)
}
