# PR 1 review and independent OS 3.0 observation

Reviewed head c426497397d4f45d93022656be78cae398f3defb (floppy-os30-boot), base393672dd36a804e8d168a12f56b98b9c3ed612f1. PR remains unmerged. Source patches were applied to a separate worktree at the matching local0.29 snapshot. Pending local memory-lab experiments were preserved.

## Findings

1. P1: src/floppy_hfe.h:64-68 finds a complete32-cell sync anywhere in the track, then unconditionally reads16 more cells for the ID. A sync at the track end causes heap-buffer-overflow in readDataByte line99. Reproduced with AddressSanitizer using a synthetic1034-byte HFE; check remaining ID length before reading.
2. P2: tools/lcd_trace.cpp:19,33 expects argc8/9 and reads the profile at argv8, although the documented interface requires argc7/8 and argv7. A default invocation prints usage; an explicit report2010 invocation silently uses hxc2026. Both reproduced at0ticks.
3. P2: tools/hfe_probe.cpp:21,30-44 initializes missing sectors with zero, writes a complete-sized image and returns success if any sector passes CRC. A synthetic two-side track containing only one valid sector produced7168bytes and exit0 with validCRC1of2. Extraction should require complete unique sector coverage or explicitly emit a partial recovery representation with missing-sector metadata.

## Independent boot observation

OS attachment:573440bytes, SHA256bc107393f3e59950ac6b87eab23c979890bbfaec8e3588c340906c0aadaf390a.
Main/scanner/PROM assets match existing0.29 fixture. GCC C++20 -O2 -Wall -Wextra; modified SIO receive suite passes locally. Native Windows and full30-target results were read from the PR, not independently rerun here.

A temporary passive observer compiled against the PR sources ran the original floppy fixture, sampled every100000ticks and stopped at13100000ticks. No firmware, media, memory initialization, controller policy or data was changed. Observer source was the existing floppy_probe with bounded run-loop and public bus/backing/latch metadata observations. Validation log:validation/pr1-os30-dram-observation.txt.

LCD:***Emulator2***/SoftwareRev.3.0 (spaces preserved in log). DMA28720transfers,28672strict DRAM writes,143360DMA WAITs. MainPC43c9, CPU completed reads3542. Active read physical/pin address98c0; backing value/known mask0/0 and merged read latch0/0; readyfalse. The backing byte itself is unknown. Thus phase/history disagreement is not established as the sole cause: DramCpuPort::readClock (dram_cpu.h:139) requires a fully known byte for readReady, and Machine also requires fully known read data to finish its strict bus cycle. Extending WAIT cannot by itself initialize this byte.

Next work must trace why the OS reads an unwritten byte (load layout, initialization, bank mapping or legitimate power-up memory use), and separate transaction completion from value certainty with an explicit policy if appropriate. Do not zero-fill memory or choose a startup phase merely to force progress. This observation establishes neither full OS load nor a silicon-accurate power-up RAM policy. No merge or remote review comment was submitted.


## Follow-up0.32

The reviewed WR3 D3 receive implementation and regression tests were selectively adopted into main with explicit DRAM power-on support. The PR itself remains open/unmerged; the three helper defects above remain outstanding and those helper sources were not adopted. docs/DRAM_POWER_ON.md records the root cause and controlled forward-progress trials.


## Follow-up0.34: HFE side layout

The real user GrandPiano HFE independently verifies all160sectors. The PR helper
uses concatenated sides, while standard HFE alternates256-byte side chunks.
Main now has a separately written strict HFEv1 importer plus independent Python
verification and corruption/coverage tests; see docs/HFE_OS31_BOOT.md. PR itself
remains unmerged and the LCD CLI defect is not repaired by this change.
