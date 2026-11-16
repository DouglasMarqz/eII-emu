# Production controller quiet periods

The digital profile in PLAY_PROFILE_062.md identified controller/source-clock
processing as the main cost, also with runtime audio disabled. The production
route unconditionally used the half-edge interpreter; only the reference route
had a complete quiet-period scheduler. This patch implements the missing
production transaction and uses the existing CPU/timer deadline scheduler.

A transaction is limited to complete rising/falling periods within one CPU
interval. It stops before the next timer/request event, phase 15, channel carry,
request/flag capture, CPU slot, reset, or any non-quiet state. Eligibility checks
the actual loaded upper/lower PROM contents, CPU request pipeline, write/ALU
clocks, DAC levels and idle DRAM port. Lower quiet phases are certified at PROM
load for each captured input code; no ROM bytes or identity are hardcoded.

Within the interval the RF2 bus value, CPU-address mux and data/request pipeline
settle once to the same values reached by the original edges. Phase, shift,
last PROM address and clock counters advance by the exact number of periods.
The final falling-edge aperture is retained. CPU instructions, WAIT holds,
DRAM transfers, DAC captures and all timer/request deadlines retain their
original order. An unknown request on any channel still rejects the production
state at the original edge; it cannot be hidden by a quiet interval.

The normal reference and per-half-edge observer routes remain available.
ChannelTimers::enableProductionSpans(false) selects the old production scheduler
for differential checks without changing publication cadence or other devices.
The filter, nonlinear solver, MIDI transport, host pacing/wait and GUI control
mappings are unchanged.

## Correctness checks

- Frozen 0.57 edge oracle: 1,114,310 edge-equivalent comparisons (including
  invalid-state holds), with 133,788 actual batched periods in
  43,478 spans. Complete observable controller state matches. Covers altered
  PROMs, CPU requests/register writes, reset, ninth slots, DRAM pins, timer
  requests and RF updates; intervals range from zero to eight periods.
- Original quiet-edge oracle: 1,024,000 comparisons, including invalid-state
  holds; 241,938 active loop iterations, PASS.
- Whole-machine synthetic differential: 1.6 million CPU ticks, old/new production
  scheduler, boundary reports, memory/known masks, both CPU pins and PCM exact.
  The span route is exercised; unknown-request and invalid-CPU stops retained.
- Random timer/ACK/reload/unknown/observer differential: PASS.
- Production invalid-state/unknown sample-CAS guards: PASS.
- AddressSanitizer/UndefinedBehaviorSanitizer span oracle: PASS with leak checking
  disabled because LeakSanitizer cannot inspect process tasks in this runner.
- Private real-firmware/media trial: matched final report+memory fingerprints
  in idle/Play, audio off/Legacy; Legacy stereo PCM fingerprints match too.
  Firmware, disk, raw lab source/samples and asset-derived logs are not shipped.

## Timing result and limits

A follow-up paired experiment used one executable and one boot state, two rounds
of five simulated seconds per mode with runtime audio disabled. The old and new
production schedulers retained the same publication/device settings.

| Mode | Old CPU seconds/simulated second | Spans | Reduction |
|---|---:|---:|---:|
| Idle | 0.942 | 0.876 | about 7% |
| Play | 0.977 | 0.931 | about 5% |

There was substantial run-to-run variability, especially Play (one round was
slightly slower). These are short native measurements, not a guaranteed Windows
percentage or a complete solution to the owner-thread CPU load. This is one
validated structural improvement; further profiling is still required. No
wall debt is discarded and no speed-meter value is changed by the patch.

Win64 GUI O3/LTO, diagnostics off, -Wall -Wextra -Werror: PASS with no
warnings. Win64 span-oracle executable cross-compiles cleanly (not run).
CMake/PowerShell include the new tests; neither build driver is executed here.
Executable hashes and validation receipts are recorded with the package. Native
container timing is not a Windows measurement. Target-machine Play CPU load
and sustained polyphonic behavior still require verification.

## Subsequent target feedback

The owner reports no visible speed gain on Windows and more CPU-thread
activity with 0.63. This supersedes any expectation based on the native timing
averages; it does not establish new thread creation. See THREAD_ACTIVITY_064.md
for the next manual-only measurement step.
