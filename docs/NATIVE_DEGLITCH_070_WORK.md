# Native DAC/deglitch development checkpoint

2026-10-22. Stable audio remains0.69, source baselineb7c23cf.
This branch is work in progress, not a new GUI release.

The resumed execution environment contained a filesystem snapshot ending
around0.51. The complete b7c23cf tree was recovered:497 blobs reused by exact
Git hash and110 fetched, all607 blobs verified. No recovery history is pushed.

`native_audio.h` preserves timestamps in1/120MHz integer units: hardware
half-edge6units,48k transport packet2500,96k step1250,192k step625. Transport
rate does not retime DAC events. The bounded collector carries a complete
initial pin snapshot, native events and explicit overflow/ordering invalidity.

The analytical nominal channel evolves the DAC reference RC, negative I/V
branch and13202/TL084 charge transfer between digital events. It supplies
output area, not just an endpoint voltage. Exponential tables replace repeated
exp calls. Switch Ron150ohm and ideal op-amp assumptions follow the existing
laboratory model; physical noise, injection, rails and finite bandwidth remain
unmodeled. Startup and recovery use explicit nominal zero charge.

Executed GCC13.3 O3/Wall/Wextra/Werror standalone tests:

* Independent1ns RK4 differential-equation oracle, three reference time
  constants: max voltage error2.15e-13V; max integral error1.98e-5V*time-units.
* Arbitrary interval splitting preserves final state and area within test
  tolerances; unknown input is guarded and nominal recovery is explicit.
* Rational packet boundaries preserve source timestamps; one hardware second
  produces exactly48000 transport frames. Overflow invalidates its frame and
  retains a complete next-frame snapshot; backward timestamps are rejected.

Integration now present (development checkpoint, not a release):

* Sparse phase/DAC callbacks, real CV code/MODE/S3528 events and bounded
  immutable packet transport to the existing DSP worker. No50ns observer is
  compiled into the GUI. Native exact-time area feeds the unchanged96/192k
  nonlinear solver. CV/VCF controls are evaluated on that solver grid.
* GUI setting, persisted NativeDacDeglitch preference, active-path status,
  manual overflow/order counters and JSONL fields. The0.69 reference route
  remains selectable; Legacy stays on its reference path.
* Sparse-event differential test:212378 events exactly match the50ns oracle,
  including timer reprogramming and varying CPU chunk lengths. Complete
  digital report and memories agree. Native observation adds zero digital
  scheduler events:151525 both enabled and disabled.
* Synthetic worker/queue PCM:3504 frames per96/192k run,3503 active and3503
  changed by S3528; repeated rendering bit exact, open phases silent, no loss.
* Real Marcato bank boot:183M ticks,520552 DMA transfers; firmware Play reaches
  S01 La Primavera / Tempo090. Six simulated seconds:288000 PCM frames,
  5875036 native events, maximum62 per packet, invalid/overflow/order/controller
  failures0; DSP/host queue drops, missing and failures0. Live peak0.0586544,
  RMS0.00877738 with the caller's calibration. Original assets remain external.
* Last9600 real event packets replay bit-exact at each solver rate. S3528
  changes9599/9600 final PCM words. No solver fallback or held sample.
* GUI crosscompiles with pinned LLVM-MinGW20261006, O3/LTO, diagnostics0,
  Wall/Wextra/Werror. This does not constitute Windows runtime/listening proof.

The initial phase scheduler unnecessarily materialized all digital counters
at analog-phase deadlines. It is replaced by independent read-only divider
histories, driven by actual OUT transitions; the original digital deadlines
are unchanged. Same-edge divider/reset transitions publish the final settled
phase, not a zero-duration intermediate level. No new firmware path is used.

PCM is bit-identical for0.5/1/2ms producer chunks. Invalid frames mute, and the
next complete snapshot/gaps restart nominal analog history. AddressSanitizer
and UBSan pass those tests (leak detection disabled because this host denies
the LeakSanitizer process/task inspection; no leak-check claim). Reference
voice PCM remains bit-identical to frozen0.68 at96/192k; Legacy remains exact.
Native packet copies initialize/copy only used event slots, avoiding a full
192-event payload clear/copy in every reference-path frame.

Linux producer CPU for6 identical simulated seconds, native LAB capture on:
initial8.699s; first detached-scheduler run6.546s; final packet-copy build6.741s.
The final reference build takes6.030s. These are host/measurement-dependent
figures and include capture overhead on native, so are not Windows performance
or a production speedup claim. A separate capture-disabled native measurement
takes6.407s versus6.030s reference (about6.25% producer overhead on this host);
its final PCM peak/RMS and complete digital report hash match the capture-on
run. Evidence retains both measurements rather than substituting a Windows
percentage. The final digital report hash is
d64cf15aad0e8209 in both paths, with matching288000 PCM frames and no transport
loss. DSP replay remains finite/nonzero with no fallback/held samples.

The GUI crossbuild is for user testing. No Windows listening/CPU measurement
has occurred here. Default reference behavior is retained; the new route is
opt-in pending absolute switched-transfer review and Windows acceptance.

Transfer-fidelity limit: the nominal switched-capacitor network is not the
old static -50 gain. LF13202 manufacturerp4 visually shows open switches at
logic0, and EII7-45 showsR26=200, R34=10k, C74/C83=470pF. Their switched
frequency response must be characterized; mathematical equation tests do not
prove measured hardware response. No compensating gain or invented constant
27.777k clock has been inserted. DAC/phases follow actual firmware timers.
Noise/injection/finite op-amp bandwidth/rails remain unmodeled.
