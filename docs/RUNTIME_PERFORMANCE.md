# Runtime performance work — unreleased checkpoint0.36

The0.35 Windows GUI boots the original OS3.1 bank and displays its LCD correctly
(user-confirmed). Its unpaced, source-edge simulation saturates a host thread
and is too slow. This is an architectural runtime problem. Two4MHz Z80s do not
justify the observed cost. This branch is NOT a completed low-CPU emulator or
a new recommended Windows release.

## Cause and reference implementations

The existing Machine runs both CPUs every250ns and the complete sample
controller at50ns half-edge boundaries. Before the 0.36e routing fix, the same conservative logic used to
verify uncertain startup histories was also on the GUI's execution path.
Choosing phase0 removes15 alternative phases but does not remove the repeated
PROM, register-bus, ALU, RAS/CAS, divider and ACK calculations.

Inspected upstream sources on2026-10-19:

* Tuth/Emulated-3 `runtime/eiii_runtime.cpp::run_until_cycle`: chooses the next
  scheduler timer, PIT output transition or caller boundary, then runs a CPU
  instruction budget. Its engine also waits when the host's audio demand is met.
* Tuth/Emulated-3 `runtime/eiii_machine.h`: functional bus handlers and a scanner
  input interface, rather than this EII gate/unknown-history simulation.
* mamedev/mame `src/mame/emusys/emu2.cpp`: two4MHz Z80 devices with MAME component
  timers. This upstream file is currently a skeleton with `MACHINE_NOT_WORKING`
  and `MACHINE_NO_SOUND`; it has no integrated micro-PROM/sample DRAM controller.
  This scope difference does not excuse the EII runtime's excessive CPU cost.

A4M-clock gprof sample before the final event refinement attributed about23%
self-time to ControllerTiming::risingClock,12% to DramCpuPort::settle and11%
to ControllerUpperPaths::rising. z80_tick itself was about2.4% self-time.
Sampling, inlining and instrumentation limit these percentages: they are not
an isolated Z80 benchmark or measurements on the user's Windows machine.

## Implemented foundations

* Raw Machine construction retains the unknown-history reference path. The
  unreleased GUI now selects the shared normal runtime preset for its explicit
  phase0/seed1 startup; unknown/all16 remains reference. See RUNTIME_ROUTE_AUDIT.md.
* Opt-in ChannelTimers::enableEventTiming schedules 8254 OUT transitions and
  HC393 request events; stopped/unknown fixed points do not require repeated
  arithmetic. Known divider histories advance algebraically in bulk. Unresolved
  evolving histories still use original edge transitions.
* Deferred counters/dividers synchronize before timer programming and at the
  end of every Machine::run call. Controller ACK clears still take effect at
  their original source edge; newly possible unknown-timer changes schedule
  the next falling edge. Each channel also synchronizes with its OLD ACK level
  before a clear-level change; a later ACK must not affect elapsed time. This
  regression was caught by adding ACK/OUT changes while time was still pending.
* Analog observers temporarily use every original half-edge, preserving phi1,
  phi2 and pre-edge observation order. This is not an event-driven audio engine.
* Single concrete upper histories update in place; branching remains unchanged.
  HC393 one-clock transitions use an immutable wiring truth table; the8254
  common one-clock decrement avoids bulk division/modulo.
* FrontPanelSession has interruptible real-time pacing and measured speed.
  Pacing limits surplus execution only; it cannot fix a core below real time.
* The pending Windows UI source enumerates/selects active WASAPI endpoints and
  queries the native mix format. No render stream is opened, started or fed.
  It therefore does not yet provide sound.

A complete-controller snapshot replay experiment was rejected and removed:
it matched the8M-clock reference output but copied/compared too much state,
getting only604397hits versus5441402misses and taking5.93s in that development
run. There is no snapshot-replay cache in the final source.

## Validation and limits

`event_timing_tests` compares the complete public timer/divider/request state
against the source-edge engine after1600 randomized programming/ACK intervals,
including writes/ACKs during deferred time, partial loads, unsupported modes,
unknown startup histories, zero/one
reloads, stopped histories, long intervals, exact request timestamps and
per-half-edge analog-observer phase equality. Standalone ASan/UBSan passes with
`detect_leaks=0`; no leak/TSAN claim.

A serial development24M-clock comparison of the original OS3.1 GrandPiano bank
produced byte-identical reports,64RF words/masks, RAM known masks and memory/
scanner checksums. Edge14.172135s versus event12.196234s on this workspace:
6s emulated in12.2s remains below real time. The event path processed3835489
network events versus60000000 source clocks. This is a single development pair,
not a median benchmark, native Windows measurement or final speed claim.
Later timer-OUT-only flush placement and per-channel old-ACK synchronization
are additional unbenchmarked changes. Do not present that development pair as
the final implementation's performance.

Prior-session frozen0.35/full200M micro-optimization comparisons and Windows
cross-compilation succeeded, but their temporary logs were lost across the
session interruption. Do not cite those as fresh final-source validation.
Fresh `validation/runtime-036-*.txt` files record the complete native suite,
ACK-fixed ASan/UBSan regression (21858events/2765005sourceclocks), final200M
GrandPiano readiness and500k unknown/all16 report equality. Final200M matches
520552DMA/518144strictDRAMwrites/97251CPUreads/12writes/276711WAIT and all three
previously frozen-reference memory hashes. That run's96.09s overlaps other
short verification and is not the final speed benchmark.
No0.36 Windows binary is released; endpoint selection and pacing still need
native Windows verification.

Reproduce after `bash tools/build-native.sh` (private assets stay outside Git):

```
build/runtime_benchmark ../roms 'path/to/GrandPiano.hfe' 24000000 0 1 edge
build/runtime_benchmark ../roms 'path/to/GrandPiano.hfe' 24000000 0 1 event
```

stdout is deterministic state/report output; stderr contains timing and event
counts. Do not run other CPU-intensive jobs during timing comparisons. HFE
import is outside the timed region. The observer does not inject media bytes
into RAM or patch firmware. Default unknown RAM/all16 phases remain available.

## Outstanding architecture work

The expensive controller still executes every source edge. Replacing that
with verified functional sample-service/DRAM/WAIT transactions is the next
substantial runtime change; timer batching alone does not achieve the goal.
Keep the edge engine as an oracle and compare firmware-visible timing, IRQs,
DMA bytes, register-file updates and DAC events. A CPU swap or a busy-loop
sleep cannot substitute for this change. Do not promote this branch as fixed,
claim0.5% host CPU, or claim a playable/audio-complete sampler.

Final-source clean serial24M pair after the ACK fix: edge13.049281s,
event11.927585s; complete stdout reports match byte for byte.3815131network
events/60000000sourceclocks. This single pair is about8.6% less wall time and
only0.503x real time: it confirms the unresolved performance problem. It does
not establish a median, Windows CPU utilization or finished runtime.
