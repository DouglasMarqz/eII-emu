# Exact037 external sampled-IP profiling 2026-10-24
No plugin rebuild, relink or source edit. DLL restored from accepted ZIP;
SHAeed34a02b789bc45a1a8dd9136a034af0e5597693f7f685e4ccceca5695c7cb1 verified.
3835COFF text ranges extracted from exact PE; boundary/relocation tests PASS.
Mapping is nearest text symbol within.text; padding may map to preceding
symbol, LTO inlines charged to containing compiled function. Raw IP histogram
preserved for future disassembly; not complete/inclusive call stacks.

Separate offline reader cross-build PASS, zero warnings, O3/LTO/originalPGO
strict mismatch flags. System/CRT imports only, no extra shippedDLL dependency.
PowerShell simple delimiterstructure check PASS; nativeWindows/WPR/TDH runtime
NOT executed here. No claim of measured hotspots/speedup yet.
Capture only exact037 module,1named machine+1DSP; validate process/thread/module
lifetime.30s own namedWPR session, finally cancels ONLY ownsession; recoveryCMD
in localworkfolder for forcedclose. Raw ETL retains systemdata locally; filtered
returnZIP only selectedTIDIPcounts/modulefilenames(no paths)/metadata/counters.
No process suspension/debugger/injection, priority/affinity/registry or sampling
interval settings. WPR external tracing introduces overhead; not speedbenchmark.
Recorder does not automatically change any permanently installed plugin.

Recipe checker: commandtokens,compilerwrapper/backend hashes and originalPGO
hash; output path only plus verifiedprofilepath may differ. Test baselinePASS,
rejectO2/missingLTO/missingPGOmismatcherror/missingstaticPASS. Does not build.
No privateprofile/ROM/manual/media/pluginDLL included in this profiler package.
No GitHub commit/push in this step; checkpoint files record continuation.

Primary Microsoft references:
https://learn.microsoft.com/en-us/windows-hardware/test/wpt/wpr-command-line-options
https://learn.microsoft.com/en-us/windows/win32/etw/perfinfo
https://learn.microsoft.com/en-us/windows/win32/etw/sampledprofile
https://learn.microsoft.com/en-us/windows/win32/api/tdh/nf-tdh-tdhgetproperty
