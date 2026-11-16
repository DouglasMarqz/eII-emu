# 0.47 physical panel contacts and host input cadence

The 0.46 GUI delivered BN_CLICKED only after mouse release and turned it into
an 800000-tick (200 ms) contact closure. A second click within that interval
extended the same closure, so firmware could not observe release/repress.

Native BUTTON subclassing now sends mouse down/up and Space down/up as actual
matrix contact commands. Capture loss, focus loss and cancellation release the
contact. Native focus, capture and pressed drawing remain in the BUTTON control.
If down/up arrive in one worker batch, a per-contact edge queue gives each
transition an executing interval (normally one5 ms quantum) instead of losing
both transitions before the scanner runs. Duplicate releases are suppressed.
BN_CLICKED no longer creates a duplicate pulse. BM_CLICK accessibility activation
retains the existing simulated pulse because it supplies no physical duration.
The scanner firmware still owns scanning/debounce; extremely short contacts may
be missed just as on hardware. No firmware task flags or LCD text are injected.

Continuous wheel/slider values coalesce per input inside contiguous pending
voltage commands, without crossing ordered events such as Boot or Step. They
are applied by the owner worker at the next command boundary, with the existing
5 ms simulation quantum. They do not force a complete snapshot for every event.
GUI refresh is 50 ms instead of 250 ms; snapshot publication is 50 ms instead of
100 ms. LCD/LED redraws still happen only when visible state actually changes.
This removes known host delays, but does not prove the user's wheel symptom is
fully resolved. Trackbar motion, actual Windows scheduling, firmware scanning
and audible response still need a Windows check. The previously observed long
audio FIFO is a separate open latency issue; no audio-buffer change is claimed.

Speed now measures actual simulation progress over at least one second instead
of >=50 ms. A 5 ms quantum is 5% of a 100 ms measurement interval; wall scheduling
and snapshot phase can therefore cause several percentage points of meter
variation. A longer average is a measurement change, not increased execution
speed. On-demand Capture state additionally reports signed wall-minus-simulated
milliseconds while running: positive means behind, negative ahead. It is not
host CPU utilization, audio device latency or a live trace.

## Validation

Release O3/LTO/NDEBUG/EII_DIAGNOSTICS=0 worker regression passes. Executing Z80
scanner observes hold beyond 200 ms, release after a 1000-tick step and a second
press/release. An 8000-event two-wheel flood retains both final independent pin
values before an ordered step. The paced 150 ms MIDI+two-wheel burst stays within
two quanta of wall time and below 15 snapshots; input wakes do not run ahead.
Existing boot rollback, reference comparison, production fault and pulse API
checks remain passing. These asset-free worker tests are not Windows UI tests.

Reproduce native firmware contact/timing experiment (POSIX, caller-owned ROMs
and HFE, no embedded assets):

```
c++ -std=c++20 -O3 -flto -DNDEBUG -DEII_DIAGNOSTICS=0 -pthread \
  -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp \
  tools/native_input_timing.cpp -o /tmp/eii-input-timing
/tmp/eii-input-timing ROM-FOLDER BANK.hfe
```

This boots the actual firmware once, then forks identical post-boot states for
10/25/50/100/200 ms Tune contacts. LCD observation has 1 ms simulated resolution.
Its boot/idle 5 ms quantum wall costs exclude audio DSP, WASAPI, MIDI callbacks,
GUI/compositor and pacing waits. Native wall timings do not predict Windows CPU
percentage or prove the reported DMA-correlated speed dip has been eliminated.

Win64 Release cross-compilation succeeds without warnings. Native Windows
mouse/Space/capture behavior and wheel/audio responsiveness remain unverified.

Recorded native result (this host, not a Windows performance claim): boot50 s
simulated /55.6966 s wall; idle2 s simulated /1.98996 s wall. Quantum median
boot5.23895 ms, idle4.81971 ms; p95 boot7.54585 ms, idle5.95199 ms. Boot is a
mixed workload, so this does not isolate DMA cost. All five Tune holds produced
TUNING / USE SLIDER A at12 ms from contact closure, invalid=0. All children
completed successfully. The fixed host pulse previously began only after mouse
release; these measurements support sending the closure on mouse down.
