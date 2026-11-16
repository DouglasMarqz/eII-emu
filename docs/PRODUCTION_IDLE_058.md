# 0.58: exact quiet-edge work and owner-thread measurement

Rapid notes and eight-note chords can still produce an unconfirmed user-reported
hang. This checkpoint does not claim to fix that fault or the separate persistent
nonlinear voice noise. No firmware, MIDI input, analog calibration, resonance
limit, solver equation, or audio buffer limit is changed.

## Shared digital path

The reference DRAM adapter already recognizes a stable CPU-address interval with
no active RAS/CAS/capture transition. The concrete production adapter now makes
that same transition directly. Address/bank/source ownership are still refreshed
on rising edges. Falling edges return only when neither that transition nor the
delayed read clock can act. Unknown ALU-address histories reject the shortcut.

A second shortcut handles only interior playback beats (phases 10..14, channels
0..7) at control word 0x004a. It checks the loaded upper PROM's quiet behavior,
checks the exact lower PROM lookup on each edge, and requires inactive request,
ACK and delayed-read paths, stable ALU clock, fully inactive DAC capture clocks,
and a quiet, non-faulted DRAM port. Request capture, channel carry, ninth slot,
reset, memory access and all other controls use the original implementation.
RF2 reads, data pipeline, CPU-address mux, source counters and write handshake
still update at every edge. No CPU or peripheral clocks are skipped. Diagnostic
shortcut counts compile out in EII_DIAGNOSTICS=0 builds.

Frozen pre-change DRAM and controller bodies are differential test oracles, not
alternative shipping engines. The new paths are checked after both edge types,
including PROM variations, CPU input changes, register writes and reset.

## Owner-thread measurement

Manual capture adds HOST ENGINE with owner-thread-busy (CPU time / wall time,
1.0 means one occupied core), last/mean/max emulation quantum wall time. A normal
quantum represents 5ms simulated time. The Windows CPU clock is GetThreadTimes,
sampled once per speed window; quantum timing uses two steady_clock calls per
quantum, not per hardware edge. A negative busy value means not yet available.
Quantum wall time includes scheduling interruptions. The speed meter remains
actual executed ticks divided by elapsed wall time; it is not clamped to 100%.

## Verification and limits

- DRAM: 1,024,000 differential cycles, 740,699 shortcut-eligible cycles, including
  private latch state and unknown-address cases.
- Controller: differential oracle after rising/falling edges, with shortcut
  execution counted independently; existing 20,000-slot reference comparison.
- Full-machine boundary publication: 1M ticks, CPU pins, memory, reports and PCM
  exact; invalid-state stop remains. Underlay and panel ownership tests pass.
- AddressSanitizer/UndefinedBehaviorSanitizer differential checks pass (leak
  detector disabled in this environment).
- Native real-firmware local trial: original bank boots; 100 successive eight-note
  chords (10ms on / 10ms off, followed by 1s simulated) reach 4,824 received/read
  MIDI bytes, zero UART overruns, production invalid=0 and advancing CPU clocks.
  This does not reproduce or disprove the reported hang. The trial is unpaced
  and its nonlinear worker drops blocks when overloaded; audio correctness is
  not inferred from the MIDI result. No asset-derived traces are distributed.
- A synthetic, valid, quiet controller loop of 10M edges measured old/new at
  0.286/0.229s, 0.325/0.215s and 0.294/0.214s in this container. This isolates
  the controller work and is not an overall emulator or target-PC speed claim.
  Full native runs vary with worker contention; no overall speedup is established.
- Win64 release cross-build uses O3/LTO, diagnostics off and warnings as errors.
  It has not been executed on Windows here. Target-machine rapid-note stress,
  new HOST ENGINE observations, and a live MIDI sequence remain necessary.

When the symptom occurs, compare CPU tick progress in two captures, host and
UART MIDI counters, owner busy/quantum time, WASAPI submission progress and chip
queue/drop/fallback/held counters. A single stopped-note snapshot cannot identify
a deadlock or prove that a filter is responsible.
