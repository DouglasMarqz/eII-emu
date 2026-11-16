$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class EiiThreadNames {
 [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenThread(uint rights, bool inherit, uint id);
 [DllImport("kernel32.dll")] static extern int GetThreadDescription(IntPtr thread, out IntPtr description);
 [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 public static string Name(int id) {
  IntPtr h=OpenThread(0x0800,false,(uint)id); if(h==IntPtr.Zero)return "";
  try { IntPtr p; if(GetThreadDescription(h,out p)<0 || p==IntPtr.Zero)return "";
   try{return Marshal.PtrToStringUni(p) ?? "";}finally{LocalFree(p);}
  } catch(EntryPointNotFoundException){return "";} finally{CloseHandle(h);}
 }
}
'@
Write-Host 'Read-only CPU audit. Close other EII instances if you want to count one instance.'
$choices = @(Get-Process | Where-Object { $_.ProcessName -match 'cubase|vsthost|reaper|ableton|bitwig|studio.?one|fl64|cakewalk' } | Sort-Object ProcessName,Id)
for($i=0;$i -lt $choices.Count;$i++){Write-Host ('{0}: {1} (PID {2})' -f ($i+1),$choices[$i].ProcessName,$choices[$i].Id)}
Write-Host 'Enter a listed host PID (or another host PID from Task Manager).'
$targetId = 0
if(-not [int]::TryParse((Read-Host 'Host PID'),[ref]$targetId)){throw 'Invalid PID'}
$hostProcess = Get-Process -Id $targetId
$targetName = $hostProcess.ProcessName
$duration = 120
Write-Host ('Measuring {0} PID {1} for {2} seconds. Keep the same idle/note conditions.' -f $targetName,$targetId,$duration)
Write-Host 'No plugin files, priorities, affinities, host state or registry settings are changed.'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outputDir = Join-Path $PSScriptRoot ('results-' + $stamp)
New-Item -ItemType Directory -Path $outputDir | Out-Null
$threadRows = New-Object 'System.Collections.Generic.List[object]'
$processRows = New-Object 'System.Collections.Generic.List[object]'
$previous = @{}
$clock = [Diagnostics.Stopwatch]::StartNew()
$lastTime = $clock.Elapsed.TotalSeconds
$lastCpu = $hostProcess.TotalProcessorTime.TotalSeconds
$cpuCount = [Environment]::ProcessorCount
for($sample=0;$sample -le $duration;$sample++){
 if($sample -gt 0){Start-Sleep -Milliseconds 1000}
 try{$hostProcess.Refresh();if($hostProcess.HasExited){break};$threads=@($hostProcess.Threads);$processCpu=$hostProcess.TotalProcessorTime.TotalSeconds}catch{Write-Host 'Host exited or cannot be queried.';break}
 $now=$clock.Elapsed.TotalSeconds;$interval=$now-$lastTime
 $namedMachine=0;$namedDsp=0
 foreach($thread in $threads){
  try{
   $threadId=$thread.Id;$name=[EiiThreadNames]::Name($threadId);$cpu=$thread.TotalProcessorTime.TotalSeconds
   if($name -eq 'EII VST3 machine'){$namedMachine++};if($name -eq 'EII SSM2045'){$namedDsp++}
   $percent=$null
   if($sample -gt 0 -and $previous.ContainsKey($threadId) -and $interval -gt 0){$percent=100*[Math]::Max([double]0,([double]$cpu-[double]$previous[$threadId]))/$interval}
   $previous[$threadId]=$cpu
   $threadRows.Add([pscustomobject]@{elapsed_s=[Math]::Round($now,3);thread_id=$threadId;thread_name=$name;cpu_seconds=$cpu;cpu_percent_one_logical_cpu=$percent})
  }catch{continue} # Thread may exit during enumeration.
 }
 if($sample -gt 0 -and $interval -gt 0){
  $processPercent=100*[Math]::Max([double]0,([double]$processCpu-[double]$lastCpu))/$interval
  $processRows.Add([pscustomobject]@{elapsed_s=[Math]::Round($now,3);interval_s=$interval;process_cpu_percent_one_logical_cpu=$processPercent;process_cpu_percent_whole_machine=$processPercent/$cpuCount;private_bytes=$hostProcess.PrivateMemorySize64;working_set_bytes=$hostProcess.WorkingSet64;thread_count=$threads.Count;named_eii_machine_threads=$namedMachine;named_eii_dsp_threads=$namedDsp})
  if($sample%10 -eq 0){Write-Host ('{0}s: host CPU {1:N2}% total, EII machine threads {2}, DSP threads {3}' -f [int]$now,($processPercent/$cpuCount),$namedMachine,$namedDsp)}
 }
 $lastCpu=$processCpu;$lastTime=$now
}
$threadRows | Export-Csv -LiteralPath (Join-Path $outputDir 'threads.csv') -NoTypeInformation -Encoding UTF8
$processRows | Export-Csv -LiteralPath (Join-Path $outputDir 'process.csv') -NoTypeInformation -Encoding UTF8
@(('host_name='+$targetName),('logical_cpus='+$cpuCount),('duration_seconds='+$clock.Elapsed.TotalSeconds),'plugin_version=owner reports0.3.7; not automatically verified','thread_cpu_percent_one_logical_cpu: 100 means one logical CPU fully busy','process_cpu_percent_whole_machine: normalized by logical CPU count','new or inaccessible threads can have blank interval CPU values; exited threads absent','thread names can be unavailable; unnamed does not mean non-EII','more EII threads may mean multiple plugin instances, not necessarily a defect','no plugin audio/input/paths/ROM/media captured') | Set-Content -LiteralPath (Join-Path $outputDir 'README.txt') -Encoding UTF8
$zipPath=$outputDir+'.zip'
Compress-Archive -LiteralPath $outputDir -DestinationPath $zipPath
Write-Host ('DONE: '+$zipPath)
Read-Host 'Press Enter to close'
