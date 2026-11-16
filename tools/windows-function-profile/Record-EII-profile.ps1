$ErrorActionPreference='Stop'
$identity=[Security.Principal.WindowsIdentity]::GetCurrent()
$principal=New-Object Security.Principal.WindowsPrincipal($identity)
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Right-click Run-EII-profile.cmd and choose Run as administrator.'}
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public static class EiiProfileNames {
 [DllImport("kernel32.dll")] static extern IntPtr OpenThread(uint access,bool inherit,uint id);
 [DllImport("kernel32.dll")] static extern int GetThreadDescription(IntPtr h,out IntPtr p);
 [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr p);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 public static string Name(int id){var h=OpenThread(0x800,false,(uint)id);if(h==IntPtr.Zero)return "";try{IntPtr p;if(GetThreadDescription(h,out p)<0||p==IntPtr.Zero)return "";try{return Marshal.PtrToStringUni(p)??"";}finally{LocalFree(p);}}finally{CloseHandle(h);}}
}
'@
Get-Process | Where-Object {$_.ProcessName -match 'vsthost|cubase|reaper|ableton|bitwig|studio.?one|fl64'} | Sort-Object ProcessName | Format-Table Id,ProcessName
$targetId=0
if(-not [int]::TryParse((Read-Host 'Host PID'),[ref]$targetId)){throw 'Invalid PID'}
$target=Get-Process -Id $targetId
$processStart=$target.StartTime.ToUniversalTime().Ticks
$plugin=@($target.Modules | Where-Object {$_.ModuleName -ieq 'EmulatorII.vst3'})
if($plugin.Count -ne 1){throw 'Expected exactly one loaded EmulatorII.vst3 module.'}
$expected='eed34a02b789bc45a1a8dd9136a034af0e5597693f7f685e4ccceca5695c7cb1'
$actual=(Get-FileHash -LiteralPath $plugin[0].FileName -Algorithm SHA256).Hash.ToLowerInvariant()
if($actual -ne $expected){throw 'Loaded plugin file differs from exact accepted0.3.7. No profiling started.'}
$selected=@($target.Threads | ForEach-Object {try{$n=[EiiProfileNames]::Name($_.Id);if($n -eq 'EII VST3 machine' -or $n -eq 'EII SSM2045'){[pscustomobject]@{id=$_.Id;name=$n}}}catch{}})
if(@($selected | Where-Object {$_.name -eq 'EII VST3 machine'}).Count -ne 1 -or @($selected | Where-Object {$_.name -eq 'EII SSM2045'}).Count -ne 1){throw 'Keep one EII plugin instance loaded, with a worker filter selected, then retry.'}
$root=Join-Path $PSScriptRoot ('profile-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,6))
$result=Join-Path $root 'result'
New-Item -ItemType Directory -Path $result -Force | Out-Null
$selected | Export-Csv -LiteralPath (Join-Path $result 'threads.csv') -NoTypeInformation -Encoding UTF8
$target.Modules | ForEach-Object {[pscustomobject]@{base_hex=('0x'+$_.BaseAddress.ToInt64().ToString('X'));size=$_.ModuleMemorySize;name=$_.ModuleName}} | Export-Csv -LiteralPath (Join-Path $result 'modules.csv') -NoTypeInformation -Encoding UTF8
$condition=Read-Host 'Workload label (for example Marcato La Primavera SSM192 buffer128 margin10 traceOFF)'
Write-Host 'Start the demo NOW. Press Enter when notes are already playing; the next30seconds must have stable settings.'
Read-Host | Out-Null
$wpr=Join-Path $env:SystemRoot 'System32\wpr.exe'
if(-not (Test-Path -LiteralPath $wpr)){throw 'Windows wpr.exe not found.'}
$session='EII037-'+[Guid]::NewGuid().ToString('N')
$etl=Join-Path $root 'local-only.etl'
('@echo off'+[Environment]::NewLine+'wpr.exe -cancel -instancename '+$session+[Environment]::NewLine+'pause') | Set-Content -LiteralPath (Join-Path $root 'Stop-only-this-profile.cmd') -Encoding ASCII
Write-Host ('Local recording folder: '+$root)
$started=$false
try {
 & $wpr -start CPU.light -filemode -instancename $session > (Join-Path $root 'wpr-start-local.txt') 2>&1
 if($LASTEXITCODE -ne 0){throw 'WPR did not start. Existing recording was not cancelled. See local wpr-start-local.txt.'}
 $started=$true
 for($remaining=30;$remaining -gt 0;$remaining--){Write-Progress -Activity 'External EII CPU sampling' -Status "$remaining seconds remaining" -PercentComplete ((30-$remaining)*100/30);Start-Sleep -Seconds 1}
 & $wpr -status collectors -details -instancename $session > (Join-Path $root 'wpr-status-local.txt') 2>&1
 & $wpr -stop $etl -instancename $session > (Join-Path $root 'wpr-stop-local.txt') 2>&1
 if($LASTEXITCODE -ne 0){throw 'WPR stop failed; raw trace remains local.'}
 $started=$false
} finally {
 if($started){& $wpr -cancel -instancename $session > (Join-Path $root 'wpr-cancel-local.txt') 2>&1}
 Write-Progress -Activity 'External EII CPU sampling' -Completed
}
$target.Refresh()
if($target.HasExited -or $target.StartTime.ToUniversalTime().Ticks -ne $processStart){throw 'Host lifetime changed. Do not use this capture.'}
$currentIds=@($target.Threads | ForEach-Object {$_.Id})
foreach($t in $selected){if($currentIds -notcontains $t.id -or [EiiProfileNames]::Name($t.id) -ne $t.name){throw 'EII thread changed during capture; keep filter/instance unchanged and retry.'}}
$target.Refresh();$endPlugin=@($target.Modules | Where-Object {$_.ModuleName -ieq 'EmulatorII.vst3'})
if($endPlugin.Count -ne 1 -or $endPlugin[0].BaseAddress -ne $plugin[0].BaseAddress){throw 'Plugin module changed during recording.'}
@(('host_name='+$target.ProcessName),('condition='+$condition),('dll_sha256='+$actual),'capture_target_seconds=30','sampling=external WPR CPU.light','profile=sampled executing instruction pointers; not inclusive call stacks','plugin_not_rebuilt=1','raw_system_ETL_remains_local_and_is_not_in_return_ZIP=1','if decoder reports zero samples or lost events do not infer hotspots') | Set-Content -LiteralPath (Join-Path $result 'metadata.txt') -Encoding UTF8
$ids=($selected | ForEach-Object {$_.id}) -join ','
Write-Host 'Recording finished. Exporting selected EII threads from local ETL...'
& (Join-Path $PSScriptRoot 'etl_samples.exe') $etl $result $ids
$decodeExit=$LASTEXITCODE
('decoder_exit_code='+$decodeExit) | Add-Content -LiteralPath (Join-Path $result 'metadata.txt') -Encoding UTF8
$zip=$root+'-RETURN.zip'
Compress-Archive -LiteralPath $result -DestinationPath $zip
Write-Host ('Return ONLY: '+$zip)
Write-Host 'Raw ETL and WPR logs stay in the local profile folder. You may delete that folder after analysis.'
if($decodeExit -ne 0){Write-Host 'Decoder could not produce usable samples. Return the small ZIP for diagnosis; do not send raw ETL.'}
Read-Host 'Press Enter to close' | Out-Null
