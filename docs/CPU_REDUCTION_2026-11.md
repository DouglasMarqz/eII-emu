# CPU reduction, November 2026 (bit-exact rounds)

Goal: lower the CPU load of the plug-in without changing one output sample.
Base commit 580588e. Every round was compared with the binary built from
580588e. Not listened to in a host: all evidence here is offline.

## How it was measured

- `tools/reference_wav_probe.cpp`: real firmware and bank, 50 s boot, then
  22 emulated seconds of fixed scenarios (idle, eight-note chord and release,
  eight single notes with different pitch and velocity, growing polyphony with
  both wheels, sliders A-D, pitch bend and mod wheel moving every 100 ms, final
  release). It writes the PCM the DSP sink delivers as a float WAV. The DSP
  queue is never allowed to overflow, so the WAV does not depend on host speed.
  Per stage it prints machine time (wall time inside `Machine::run`) and DSP
  thread CPU time, both as host seconds per emulated second (1.0 = one core).
- `tools/machine_fingerprint_probe.cpp` (already in the repo): 18 stages incl.
  sampling/record; DRAM, known mask, scanner RAM, register file, PCs, LCD and a
  hash of every native audio frame.
- Gate for each round: fingerprint text identical and five WAVs identical
  (MD5) to the base binary: EIII custom native, SSM2045 model 2 192k native,
  Legacy+Q native, EIII custom fast route, historical BasicVoice.
- `tools/sampling_profiler.h` + `tools/profile_report.py`: in-process sampler
  (suspends the threads about once per ms, unwinds, resolves inlined frames
  with llvm-symbolizer). No admin rights, no ETW. Build the probe with `-g`.
- Timing pairs run the old and the new binary at the same time. The PC
  switches between a fast and a slow state (a media player was running; the
  same binary measured 0.71 and 1.03), so single runs are not comparable.

Reference WAV MD5 (base = every round):

| Configuration (mode native recon) | MD5 |
| --- | --- |
| 5 1 1 EIII custom, hardware DAC route | 2582077f3558029e2097b9056183bb92 |
| 7 1 1 SSM2045 model 2 192k | d2f3ce61d1725f19d30b3ab41087dbc1 |
| 4 1 1 Legacy + Q | 9be29be443003458a2c027eaa5a8eb8b |
| 5 0 1 EIII custom, fast route | 1f3d456aff2f0b235cf1567d1773c3c8 |
| 1 0 0 BasicVoice | d6e2a7f9a8a826cafea83ea233839a73 |

## Where the CPU goes (base, O3/LTO, EIII custom, native route)

Two threads do the work: the machine thread (both Z80s, controller, timers)
and the DSP thread. Nothing spins while waiting: the engine waits on the DSP
signal and the host target.

| Thread | Host s per emulated s |
| --- | --- |
| Machine | 0.71-0.73 (0.65 with the shipped PGO profile) |
| DSP, EIII custom / Legacy+Q | 0.13-0.19 |
| DSP, SSM2045 model 2 192k | 0.54 |

Machine thread, share of its own time (sampled, play scenarios):

| Share | Block |
| --- | --- |
| 58 % | Voice controller: 10 MHz PROM sequencer edges, service bus, timer events |
| 18 % | Main Z80 boundary (`step`): the Z80 core itself is about 3 %, the rest is bus, WAIT, CTC, SIO/DMA daisy and DRAM pins |
| 14 % | Scanner Z80 boundary and PIO link |
| 6 % | Native audio events (phase events through the sink, frame collector) |
| 4 % | Rest of the run loop |

The profile is flat: no function holds more than 13 % of self time.

Controller edge counts over the 22 s (220 M source clocks):

- 127 M real rising edges (58 %), 18.9 M single quiet edges, 74.1 M clocks in
  47.5 M quiet spans (1.56 clocks per span: a span cannot cross a CPU tick).
- The eight channel timers run and request at about 26 kHz each also with no
  note held, so idle and play cost the same. 19 % of the voice slots serve a
  request.
- An idle voice slot still needs three real edges (phases 8, 9, 15); the
  ninth slot is always real (24.4 M edges).
- The main CPU has a DRAM request active on about 4.5 % of the edges.

## Rounds (all bit exact)

| Round | Change | Paired result |
| --- | --- | --- |
| 1 | `Machine::stepNode<bool>`: main and scanner boundaries compiled as two straight bodies | play 0.727 -> 0.701 (3 pairs) |
| 2 | `PromConsensus::lookupKnown` direct byte table; channel step with -CHAK disabled only moves the selected request input | 0.678 -> 0.667 (1 pair) |
| 3 | `DramPort::settle` idle shortcut; one selected request level per edge | see totals |
| 4 | one physical-address computation at the end of the main boundary | inside noise, kept |

## Totals against 580588e

Machine thread, host seconds per emulated second, EIII custom on the hardware
DAC route. Alternating solo runs of the old and the new binary (the first
timing pairs ran both at once; the faster binary then finishes its boot
earlier and the slower one plays alone at a higher turbo clock, which biased
the play stage against the new binary, so those pairs are not used here).

| Build | Stage | 580588e | now | Change |
| --- | --- | --- | --- | --- |
| O3/LTO, no PGO (2 rounds) | boot | 0.842 | 0.762 | -9.5 % |
| O3/LTO, no PGO (2 rounds) | play | 0.751 | 0.656 | -12.7 % |
| O3/LTO + local PGO training (3 rounds) | boot | 0.649 | 0.570 | -12.2 % |
| O3/LTO + local PGO training (3 rounds) | play | 0.582 | 0.523 | -10.2 % |

Spread between rounds was below 1 % except one PGO play run of the old binary
(0.559 against 0.594 and 0.593). The DSP thread is unchanged (0.13-0.16).
PGO here means: `custom_eiii_pgo_training.cpp` trained separately on each
source tree (ssm192, legacy, eiii, model2), then the probe built with
`-fprofile-instr-use` and `-Werror=profile-instr-out-of-date`. It is the
shipped recipe applied to the probe, not the plug-in DLL.

Project suite (`tools/run_suite_win64.py`) on the final tree: 99/99 PASS.

## Tried or examined, not kept

- Whole idle-slot kernels (phases 8, 9, 15): the general edge under idle
  preconditions already skips the DRAM and DAC work; a kernel would save
  little per edge. Not written.
- Lazy controller (run it only when the CPU or audio observes it): the per
  edge inputs (read level, CPU address) would have to be recorded and
  replayed; estimated gain about 7 % for many new flush points. Not written.
- Machine/engine threads: no busy waiting found, nothing to remove.

## What is left

- The controller is still about half of the machine thread. A larger gain
  needs transactions that cross CPU ticks (the lazy controller above) or a
  cheaper ninth slot; both need their own differential oracle.
- Timer events (about 1 M per second) and the native audio event path (6 %)
  were not touched; `runtimeEvents` is read by the sampling path, so its
  count must stay exact.
- DSP thread untouched. SSM2045 model 2 at 192k costs 0.54 of a core.
- The owner's PGO profile (623bf0f) no longer matches the changed functions.
  The strict recipe (`-Werror=profile-instr-out-of-date`) needs a new
  profile: `tools/pgo-train-win64.cmd` locally, or a new owner capture.
- Nothing was listened to in a host.

## Plug-in build of 8990d20 (2026-11-14)

- Profile: `tools/pgo-train-win64.cmd` on 8990d20 (ssm192, legacy, eiii,
  model2, all rc=0), `build/pgo-train-8990d20/eii.profdata`, SHA256 7cb49b88...1ae5da1.
- Build: `tools/build-win64-vst3.ps1 -ProfileData ... -RomFolder -Floppy`,
  strict recipe unchanged. Engine, Legacy Q, sampling monitor, input and
  filter-service lifecycle tests PASS; Steinberg validator 47/47.
- DLL: `build/win64-vst3-pgo/EmulatorII-Custom.vst3`, 3,388,416 bytes, SHA256
  e8707e9ab4ad8994c17230d11846874f56c8722dd60a8ffe554667d0bc0efeed.
- Headless host test: first 2 FAIL, both faults of the test, not of the
  plug-in (the previous DLL failed the same way):
  - "editor size stored in the state" still expected `editor=900x349`, but
    `editorTest` resizes to 1100x427 since the editor minimum width became
    1000; the state correctly holds `editor=1100x427`.
  - "each instance owns its machine thread": both instances live for about
    80-200 ms on this PC, and the watcher (20 ms polling, system-wide thread
    snapshot) saw 0 or 1 named threads. Counted from inside the workers the
    two "EII VST3 machine" threads are there. The workers now count once
    while both instances are known to be running.
  After the test fix: RESULT PASS 0 failures, two runs in a row. All audio
  checks give identical figures for the old and the new DLL (MIDI57
  111.816 Hz, MIDI60 132.393 Hz, MIDI69 221.755 Hz, level 0.0808943).
  Offline speed 1.70x -> 1.84x real time.
- Not installed anywhere and not listened to in a host.
