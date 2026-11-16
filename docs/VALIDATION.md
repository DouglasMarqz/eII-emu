# Validation: EII bring-up 0.1

Date: 2026-10-17. Linux GCC 13.3.0, C++20, optimized build. Windows x64 products
cross-built with LLVM-MinGW 20261006 UCRT (clang 23.1.3).

## Executed tests

- Synthetic CPU/DMA bank separation, reset-page aliases, ROM write protection,
  scanner ROM/RAM aliases: PASS.
- IC135/IC137 input pin mapping and old-state feedback/simultaneous latch: PASS.
- Synthetic Z80 instructions drive the LCD data/command bus: PASS.
- Serial input consumption and character pacing: PASS.
- The synthetic tests under AddressSanitizer and UndefinedBehaviorSanitizer: PASS (leak scanning disabled for the container).
- Main 2.1 + scanner 3.1: original scanner boot banner and `Boot in Progress`.
- Main 2.3 + scanner 3.1: same scanner milestone.
- Main 2.1 + scanner 2.1: same scanner milestone.
- Diagnostic EPROM + scanner 3.1: original monitor reports `Static RAM OK`.
- Diagnostic `HE` and `HE TE`: original firmware responds with command/test help.

These are **not** successful complete OS boots. Main firmware still waits on
unimplemented devices. Boot banners validate the scanner/LCD path, not disk I/O.
The diagnostic's SRAM message validates its emulated test under this model;
it does not certify the entire hardware, dynamic-memory arbitration or audio.

## ROM identities used

| Role | SHA-1 |
| --- | --- |
| Main 2.1 | a2332266d6a12eade584b5da85817f3b3ed9037c |
| Main 2.3 | 4ec4bf6df95638d4503ab8ff7e859c420ec0720e |
| Scanner 3.1 | e354388cd694a1ce55a5b7d89c546308628090c7 |
| Scanner 2.1 | 4485e5423606014be73f121c2ee1f053a6eb148a |
| Diagnostic | d572d88c8788e25154a3bb8fd2ba3ff8a300ae40 |
| IC135 PROM | a2adbfb412897307277b331cbda2c250def6d172 |
| IC137 PROM | 0bd6a6acab799231454c001b1d600000f78de345 |

The ROM importer uses SHA-256, with full values in tools/rom_catalog.json.
ROM files and disk images are not included in this source/binary package.

## Windows limits

GUI, headless probe and synthetic-test executable: cross-build only. Native
Windows launch, GUI layout, file dialog and PowerShell helper execution are not
validated on a Windows host in this environment. No Windows audio engine exists
in this milestone. All file import is local and no firmware is downloaded.

## Missing systems

Full PIO handshakes/interrupts; floppy SIO/DMA transfer; complete microcoded
sample memory controller; DAC/sample playback, control-voltage filtering,
SSM2045/S3528 audio model, sampling, MIDI performance and VST3.

## CTC IRQ continuation, 2026-10-17

Native shell/GCC build: PASS. Synthetic CTC OUT/IN, delayed EI, held interrupt,
IM2 vector acknowledgement and repeated RETI handlers: PASS. Reverting only
the INT-preservation correction fails the held-request assertion. Sanitizer
run and diagnostic HE TE are recorded in validation/*ctc*.

Main 2.1/scanner 3.1, 40M clocks: first main PIO A write 97h at clock
18876512; no following scanner A read within the budget. See
validation/pio-ctc-40m.csv, validation/pio-ctc-40m.txt and docs/PIO_BRINGUP.md.
No complete OS boot. CMake could not be run because it is not installed.

## PIO 0.3 and analog laboratory 0.4

PIO mode-2 first-transfer, backpressure, reverse transfer, A/B and upstream
CTC priority, IM2 vectors, held requests and RETI tests: PASS. Main 2.1 with
scanner 2.1 or 3.1 reaches `Insert Diskette` at 40M clocks, with 32 transfers
and paired IRQ ACKs. Main 2.3 still polls low port 3Dh in the same budget.
See docs/PIO_BRINGUP.md and validation/*pio* / pio-mode2-40m.* for evidence.

Standalone SSM2045 open-loop small-signal filter equations, capacitor ratios,
typical FC sensitivity ratio, ideal linear VCA branch and R154/C161 impedance
tests: PASS. These are numerical reference tests; there is no hardware
calibration, resonance/nonlinearity, complete S3528 or integrated audio yet.
See docs/ANALOG_LAB.md and validation/*analog-lab*.

## Control-voltage bus 0.5

Actual Z80 OUT writes to main mux/DAC ports and aliases drive all three
ideal control-voltage banks. Twenty-four-channel routing, bank inhibit/hold,
DAC endpoints, simultaneous enables and unknown power-up tests: PASS.
Native suite and ASan/UBSan control tests: PASS. See
docs/CONTROL_VOLTAGE.md and validation/*control*.

Original main 2.1/scanner 3.1 still reaches Insert Diskette at 40M clocks;
it has zero control-DAC/mux writes in that run. Diagnostic HE TE remains
functional. No OS envelopes, settling calibration or integrated audio claim.

## Channel timers 0.6

8254 binary mode-2 loading, one-clock pulse width, deferred live reload,
byte-access modes, maximum count, all nine board routes/aliases, exact
10MHz/4MHz clock ratio and Z80 integration tests: PASS. Native suite and
ASan/UBSan timer tests: PASS. See docs/CHANNEL_TIMERS.md and validation/*timers*.

Main 2.1/scanner 3.1 at 40M CPU clocks: Insert Diskette retained, exactly
100M timer-input clocks, eight real-ROM divisor-360 counters producing
269866..269870 pulse pairs, ninth timer unknown/unconfigured. Diagnostic
HE TE retained. Channel request/sample/PROM transport remains disconnected.


## Output request timing 0.7

Native suite and request timing ASan/UBSan: PASS. Startup-state convergence,
channel 0 vs 1..7 feedback taps, settled switch phases, asynchronous ACK
priority, independent elementary counter reference and 100ns pulse capture
inside CPU frames covered. See CHANNEL_REQUESTS.md and validation/*requests*.
Original main 2.1/scanner 3.1: Insert Diskette retained at 40M CPU clocks;
all eight dividers synchronized, 269866..269870 guaranteed request clocks,
CHRQ unknown because PROM-driven CHAK is not yet connected. Diagnostic HE TE
retained. Counts are not sample transfers. No OS boot, integrated audio,
physical timing calibration or new Windows runtime verification.

## Channel service routing 0.8

Native suite and service ASan/UBSan: PASS. HCT151 request selection, rotated
HCT138 ACK decode (exhaustive partial-known select/enable truth table),
unknown-input isolation, all-eight latch clear/request feedback and source-edge
UREQ propagation covered. Main 2.1/scanner 3.1 retains Insert Diskette at40M
CPU ticks and diagnostic HE TE retains Static RAM OK/help. Controller inputs
remain unknown in Machine until scan/state counters and input/PROM latches
are clocked. No completed PROM service, sample transfer, drive implementation,
OS boot or audio claim. See CHANNEL_SERVICE.md and validation/*service*.
Drive identity/reference-document findings are in FLOPPY_HARDWARE.md; the
manufacturer model for E-mu ZM302 has not been conclusively cross-referenced.

## Controller timing 0.9

Native seven-target suite and controller ASan/UBSan: PASS. Modulo-8 state
reload, modulo-9 scan, delayed carry shift, gated old-state input capture,
partial unknowns, reset scope and all-phase consensus against 16 concrete
startup trajectories covered. Five 50ns half-edges perCPUtick distinguish
rising controller and falling timer/divider edges. Main2.1/scanner3.1 retains
Insert Diskette at40M CPUticks with100M rising and200M half-edges; request
counts unchanged. DiagnosticHE TE retained. PROM sequencing, memory service,
absolute phase/delay calibration, disk boot/audio and newWindows runtime
remain unfinished. See CONTROLLER_TIMING.md and validation/*controller*.

## Clocked original micro-PROMs0.10

Native eight-target suite and PROM ASan/UBSan: PASS. All19683 partial-input
PROM patterns checked against full address enumeration; old pipeline,
counter/feedback lookup, missing/reloaded chip, CPU request/completion/wait FFs
and delayed UCD08.2 acknowledge covered. Original IC135/137 recovered and
hashes match catalog. See CLOCKED_PROM.md and validation/*prom*.
CPU handshake is observation only: WAIT does not stall Z80 and direct memory
access remains. Register files/ALU/DRAM service, sample transfer/audio,
physical timing calibration, OS boot and new Windows runtime remain absent.

With both originalPROMs, main2.1/scanner3.1 retains Insert Diskette at40M
CPUticks and100M PROM clocks; diagnostic HE TE retains SRAM/help at4M.
Final control known-mask0600h demonstrates remaining unknown inputs/phase;
representative control values are not complete known microinstructions.

## CPU register staging0.11

See CONTROLLER_REGISTERS.md. Native nine-target suite and register component
ASan/UBSan pass. Exhaustive729 partial-address reads checked independently;
real Z80 held-write capture occurs on strobe deassertion, once per OUT.
Source logs: validation/tests-controller-registers.txt and
validation/sanitizer-controller-registers.txt. Scheduled SRAM/ALU/DRAM service
and actual CPU WAIT remain absent; this milestone adds CPU staging plus a
standalone register-file component only. No Windows execution verified.

Original main2.1/scanner3.1 +both PROMs at40M CPUticks retains Insert Diskette,
32 PIO transfers and100M PROM clocks. All four staging latches receive32
writes each; final address31/known63, data10257/known16777215. Diagnostic
HE TE at4M ticks retains Static RAM OK/help and leaves staging unknown.
See validation/registers-main21-scan31.txt and registers-diag-testhelp.txt.


## Register write port0.12

Native nine-target suite passes; final targeted register/reset timing and
ASan/UBSan checks logged in validation/final-register-write-port.txt and
sanitizer-register-write-port.txt. Synthetic PROM validates known data commits
through IC92/RFWR across all16 startup trajectories, with no direct OUT commit.

Original main2.1/scanner3.1 plus both PROMs at40M CPUticks retains Insert
Diskette and32 PIO transfers. Each trajectory records1388920 certain RFWR
apertures. SRAM fully known cells remain0/64 because the ALU/RFD read feedback
is still missing; aperture counts do not prove correct complete register
initialization or sample transfers. Diagnostic HE TE at4M ticks retains SRAM
message/help. See validation/writeport-main21-scan31.txt and
writeport-diag-testhelp.txt. No Windows runtime, actual CPU WAIT, OS boot or
audio validation added.

## RFD read bus and pipeline0.13

Native ten-target suite and RFD ASan/UBSan pass. New tests independently
check all729 ternary three-driver enable/data patterns, floating/contention,
all data/flag lanes and masks, RAW UCD04 sampling, CPU write-closing bus
capture, SRAM reads into pipeline/IC140 and16-startup-phase consensus.
See RFD_PIPELINE.md and validation/tests-rfd-pipeline.txt plus
sanitizer-rfd-pipeline.txt.

Original main2.1/scanner3.1 plus both PROMs at40M ticks retains Insert Diskette,
32 PIO transfers and100M PROM clocks. Now16/64 register SRAM cells are fully
known across all trajectories; this is not complete ALU correctness. Final
sampled pipeline known-mask can be zero, depending current bus ownership.
Diagnostic4M HE TE retains Static RAM OK/help. See validation/rfd-*.txt.
ALU data, actual DRAM/CPU WAIT, OS boot, audio and Windows runtime remain absent.

## Controller ALU0.14

Native eleven-target suite and ALU ASan/UBSan pass. Known sum/flag pin
mappings checked against independent arithmetic; partial bits checked for
soundness by exhaustive compatible-word/control enumeration. DECCTL81
ternary cases, gated latch hold/edge/uncertainty/reset and synthetic repeated
ALU-to-SRAM increments covered.16-path ALU/writeback equals16 concrete
phase runs. See CONTROLLER_ALU.md and validation/tests-controller-alu.txt,
sanitizer-controller-alu.txt.

Old focused register/RFD tests may still label their own fixtures 'ALU absent';
the new ALU test validates the connected arithmetic/writeback path separately.
Actual DRAM/sample transport, CPU WAIT, OS boot and audio remain unverified.

Original main2.1/scanner3.1 +both PROMs40M ticks retains Insert Diskette,32
PIO transfers,100M PROM clocks and16 fully known register cells across startup
trajectories. Final ALU known-mask remains0; its representative value is not
a proven address/carry. Diagnostic4M HE TE retains Static RAM OK/help.
See validation/alu-main21-scan31.txt and alu-diag-testhelp.txt.

## CPU DRAM/WAIT0.15

Native twelve-target suite and CPU DRAM ASan/UBSan pass. Real Z80 synthetic
programs wait, write once, read through IC103 and continue in all8 banks /
16 startup paths. Local memory bypass and unknown-data stalls are covered;
scanner/controller clocks continue. See CPU_DRAM.md for Z80 bus adapter and
conservative readiness guard timing limits.

Original PROMs pass the isolated quiet-CHRQ controller fixture in dram_probe
across16 explicit phases/all8 banks, including late WE and IC103 ordering.
This is not full Machine startup validation.

**Checkpoint0.15 strict boot limitation (historical):** original main2.1/scanner3.1 no longer
reaches the historical direct-memory Insert Diskette milestone in this mode.
It stalls at its first DRAM write, PC0ad7, with unknown PROM/WAIT state and
zero committed CPU DRAM cycles. A bare synthetic CPU self-check with original
PROMs also stalls. These are unresolved startup/correlation results, not
successful DRAM firmware tests. Quiet inputs were not injected into Machine.
Diagnostic HE TE retains Static RAM OK/help (local SRAM path). Logs:
validation/dram-*.txt, tests-cpu-dram.txt and sanitizer-cpu-dram.txt.


## Correlated CPU DRAM/WAIT0.16

Thirteen native test targets pass. New independent Boolean-history
enumeration verifies exact upper feedback/UREQ/pipeline/completion/WAIT
state sets across192 clocks; peak eight histories in that fixture. The real
Z80 bank test now also uses a feedback-dependent CPU/channel priority PROM,
with unknown channel requests and all sixteen startup phases. CPU DRAM and
upper-history tests pass ASan/UBSan.

The external-image controller fixture passes every explicit phase and all
eight banks with both quiet and unknown CHRQ: aggregate write208/read192
source clocks over32 write/read fixtures. Original-PROM synthetic CPU self-
check now writes/reads once and HALTs atPC000e, ten WAIT hold clocks in10K CPU
ticks. Diagnostic4M HE TE still reports Static RAM OK/help, zero DRAM cycles
and zero WAIT holds. See validation/*correlated*, sanitizer-upper-paths.txt
and dram-original-prom-request-histories.txt.

Full sample DRAM arbitration/backend, disk transport, OS boot and audio remain
unimplemented. Channel request/ACK and lower datapath correlations still use
conservative consensus. No Windows or physical subcycle timing claim.


Original main2.1/scanner3.1 with both supplied PROMs now reaches **Insert
Diskette in strict DRAM/WAIT mode** at40M CPU ticks. It completes7 CPU DRAM
writes and12 reads, with95 WAIT hold clocks,32 main-to-scanner PIO transfers
and paired interrupts. All16 startup phases remain represented;100M PROM
clocks execute. This supersedes the0.15 first-write stall. See
validation/dram-correlated-main21-scan31.txt. The following arithmetic-work
optimization and branch-count report were covered by the final DRAM/native
and sanitizer tests; the40M run used the same controller semantics before
that nonbehavioral optimization. Scanner2.1 and main2.3 have not been rerun
in strict mode.


## Playback DRAM and digital DAC path0.17

Fourteen native targets pass, including sample_transport_tests. Real Z80 OUT
instructions initialize eight address registers; the controller's RF/RFD/ALU/
ALUGO/row-column/IC40/ACK path delivers a distinct fixture byte from each bank
to its channel DAC latch. All16 concrete startup phases and their envelope
pass, with a concurrent strict CPU write/read/continue. Unknown sample data
replaces old DAC certainty without corrupting other channels, and a later
known byte propagates again. Unit tests also cover IC40 old-MDO capture,
rotated rising ACKs/unknown-edge merges, idle CPU row preservation, bank reset,
write inhibition and absence of unsupported sample-write side effects.

The independent upper Boolean-history reference still passes (peak12 histories
with the additional memory/output-clock states). Stored DAC outputs merge
only when future controller/DRAM/ACK-clock state agrees; no DAC/SAR analog
feedback is claimed. See docs/SAMPLE_TRANSPORT.md and validation/*sample*.


Sample transport and CPU DRAM regression ASan/UBSan pass. Original-PROM CPU
fixture still passes quiet/unknown requests (write208/read192 source clocks).
Original-PROM CPU self-check HALTs000e with1 write/1 read/10 WAIT holds in10K
CPU ticks. Diagnostic4M HE TE retains Static RAM OK/help with0 DRAM/WAIT.

Original main2.1/scanner3.1 with both PROMs at40M CPU ticks retains Insert
Diskette,7 CPU writes/12 reads/95 WAIT holds and32 paired PIO transfers.
100M PROM clocks execute;34 final upper histories represent16 startup phases.
IC40/DAC stored codes are still unknown in this original boot snapshot,
consistent with no loaded OS/sample playback. Successful test-fixture data
transport is not original-firmware audio playback. No OS/audio/Windows claim.


## Am6072 and analog experiment bridge0.18

Fifteen native targets pass. The ideal Am6072 law matches manufacturer chord/
step data for all256 codes and independent enumeration for all6561 partial
masks. Tests cover sink-pin steering, two zeros, reference multiplication,
board I/V polarity, unknown/invalid inputs and explicit stage/MODE guards.
Both signs' impulse transfers at100/1000/5000Hz agree with an independent
complex product for the diagnostic reference/I-V/filter/C93/VCA/feedback
cascade. The two signs have different feedback-RC paths as wired on the board.

Expanded real-Z80 sample_transport_tests writes all eight amplitude-CV holds
through actual OUTs, observes Machine sample/CV latches and processes them
through the explicitly initialized diagnostic bank. All eight nominal DAC
voltages and finite known filter/VCA observations pass. Am6072/analog and
expanded sample-bridge ASan/UBSan pass. See validation/*playback*.

Diagnostic4M HE TE remains Static RAM OK/help with0 DRAM cycles/WAIT holds.
The snapshot API does not change CPU/controller/source-clock execution;
40M original main boot was not rerun for0.18. The strict Insert Diskette result
recorded for0.17 remains prior evidence, not a new0.18 runtime claim.

Active deglitch, S3528, full controls/nonlinearity and timed host audio remain
incomplete. Defaults require missing external nodes rather than bypassing
them. The direct diagnostic routes and known-zero capacitor initial conditions
are explicit, and no original-firmware sound or OS boot is claimed.
See docs/PLAYBACK_ANALOG.md for primary references and exact scope.

## Checkpoint0.19 — switched analog kernels

2026-10-18, Linux GCC13/C++20. `bash tools/build-native.sh`:16 test targets
PASS. Refreshed `switched_analog_tests` including clock/cascade additions PASS;
ASan/UBSan PASS with `ASAN_OPTIONS=detect_leaks=0`. See
`validation/tests-switched-analog.txt`, `validation/test-switched-cascade.txt`
and `validation/sanitizer-switched-analog.txt`.

Independent nodal RK4 verifies phase-resolved3202/TL084 charge transfer.
Actual settled HC393 phases drive acquisition/transfer at100ns boundaries.
Tests cover S3528 CE semantics/all64 programming codes, divider cadence and
uncertain phase, nine independently calculated surrogate frequency magnitudes,
real Z80 all-eight filter writes at strobe release, and a20ms source-edge
DAC/deglitch/SSM/S3528/VCA experiment without diagnostic direct bypasses.

The S3528 reconstruction is explicitly a seventh-order elliptic surrogate,
not recovered AMI coefficients or a match to every typical response point.
The deglitch kernel uses nominal Ron/ideal linear TL084; physical switch/op-amp
compliance remains uncalibrated. Details: `docs/SWITCHED_ANALOG.md`.

Diagnostic original ROM plus scanner3.1/both PROMs,4M CPU clocks, `HE TE`:
Static RAM OK/help retained,0 CPU DRAM cycles and0 WAIT holds. Log:
`validation/switched-diag-testhelp.txt`. No CMake/Windows runtime validation.

Original main2.1/scanner3.1 plus both supplied PROMs,40M CPU ticks in strict
DRAM/WAIT mode: **Insert Diskette**,7 writes/12 reads/95 WAIT holds,32 paired
PIO transfers,100M PROM rising edges and34 final upper histories retained.
Each main port40h..47h is written once with3Bh (N10, nominal12.5kHz at5MHz).
The firmware trace and final IO counters agree; filter-code capture semantics
are independently exercised by the real-Z80 strobe test. Logs:
`validation/switched-main21-scan31.txt`, `validation/switched-filter-writes.txt`.
This is boot-ROM initialization, not disk OS boot or sample playback.

## Checkpoint0.20 — Machine-clocked playback and capture arithmetic

2026-10-18 Linux GCC13/C++20. `bash tools/build-native.sh`:17 native targets
PASS, no compiler warnings in the recorded log. Clocked integration ASan/UBSan
PASS with leak scanning disabled for the container. The shared synthetic
transport PROM helper was moved unchanged into tests/transport_fixture.h.

Real Z80 sample/RF/ALU/DRAM/DAC/CV/timer/S3528 writes drive480 eight-channel
frames over10ms. Full versus400x100-CPU-clock runs produce bit-identical frames;
attached/detached/chunked digital reports match. Independent rational-overlap
reference checks44100/48000/96000Hz frame areas, counts and unknown spans.
Reprogramming, unknown startup, direct-bypass absence and impossible linear
supply excursions are covered. Initial capacitor/phase/pin conditions are
explicit experiments, not firmware initialization or hardware calibration.
Logs: validation/tests-clocked-playback.txt and
validation/sanitizer-clocked-playback.txt.

`audio_probe` original diagnostic ROM capture produces120 frames in2.5ms at
48kHz, each known_mask0 with blank voltages. CLI guards reject unsigned rate
wrap, negative counts, trailing junk, nonfinite gm and out-of-range VCA current.
Logs: validation/audio-probe-unknown.txt and tests-audio-probe-cli.txt.

`python tests/measure_tone_tests.py`: PASS. Independently generated997Hz sine,
0.25 gain/123us delay, DC offsets and known second harmonic verify reported
gain/phase/DC/residual. Invalid/short/reversed/nonfinite/unresolved-input data
are rejected. Log: validation/tests-measure-tone.txt. This tests capture math;
no physical hardware data was supplied or used.

Original diagnostic main/scanner3.1 plus both PROMs,4M CPU clocks, HE TE:
Static RAM OK/help retained and0 DRAM cycles/WAIT holds. Log:
validation/clocked-diag-testhelp.txt. CMake, Windows, real-time performance,
production anti-aliasing and calibrated host audio are not validated here.
See docs/CLOCKED_PLAYBACK.md for model scope and measurement provenance.

Fresh0.20 original main2.1/scanner3.1/both PROMs40M CPU ticks, engine detached:
Insert Diskette;7 CPU DRAM writes/12 reads/95 WAIT holds,32 paired PIO transfers,
100M PROM clocks,34 final upper histories. Log:
validation/clocked-main21-scan31.txt. This verifies the default digital path
following the shared clock-loop refactor; attached-engine equivalence is the
synthetic real-Z80 comparison above, not a fresh40M attached run.


## Checkpoint0.21 — AMI typical-curve fit

2026-10-18 Linux GCC13/C++20:18 native targets PASS with no compiler warnings.
New fitted-profile tests and the continuous real-Z80 playback integration both
PASS ASan/UBSan. Logs: validation/tests-s3528-curve-profile.txt,
sanitizer-s3528-curve.txt and sanitizer-clocked-curve-profile.txt.

The executing C++ kernel is driven with tones, using fitted complex output
and neighboring-frequency phase differences. Against10 plot points excluded
from optimization: maximum delay error12.2793us versus375.447us for the generic
profile; maximum stopband error2.06237dB. Relative passband max0.09498dB over
the native measurement grid;1.06fc gain about-3.93976dB relative to0.6fc.
These are matches to manufacturer typical plots, not physical calibration.

Reproducible source pixel references and split are in
data/s3528b_1993_reference.json; report/summary in
validation/s3528-curve-fit.json and s3528-curve-summary.txt. Regenerating the
extraction from the exact source PDF produced identical JSON. The source SVG
comparison was rendered and visually reviewed. CSV metadata identifies default
ami1993_typical_curve_fit versus selectable generic_elliptic; both CLI checks
PASS in validation/tests-s3528-profile-cli.txt.

No fresh40M boot was run for0.21: the default engine-detached CPU/controller
execution path is unchanged, with0.20 evidence retained. Current18-target
native checks include attached/detached real-Z80 digital-report equivalence
and exact capture block invariance with the new default profile. No CMake,
Windows, real-time or actual EII hardware calibration claim. The assumed
normalized3.2kHz response does not certify every cutoff/code or the high end.
See docs/S3528_CURVE_FIT.md for reading allowances and residual limitations.


## Checkpoint0.22 — ALU shared-control consensus

2026-10-18 Linux GCC13/C++20.18 native targets PASS with no compiler warnings;
ALU ASan/UBSan PASS. New regression first failed against the0.21 implementation,
then passed after the fix (expected negative control in
validation/alu-shared-control-before.txt).

Shared UCD04.1B must be the same value at both A8/A9. B=0x100,CI=A4a=0,
A4b unknown yields0x100 or0x400: bit9 is certainly zero, previously lost by
bitwise uncertainty propagation. The new implementation enumerates at most8
compatible shared-control assignments and performs exact fixed-addend carry
propagation before consensus. Known-word/unknown-word fast paths remain.

26244 partial cases spanning four carry/pin-boundary groups and three base
patterns now require EXACT known-mask equality with independent full integer
completion enumeration. Existing known arithmetic/flags/DECCTL/latch order,
connected writeback and16-phase envelope regressions pass. Logs:
validation/tests-alu-correlations.txt, tests-alu-shared-controls-suite.txt and
sanitizer-alu-correlations.txt. Service PDF85/7-22 shared pins visually rechecked.

Original diagnostic ROM/scanner3.1/both PROMs4M HE TE: Static RAM OK/help and
0 CPU DRAM cycles/WAIT holds, validation/alu-correlations-diag-testhelp.txt. Local mask
exactness is not full controller-history or physical timing calibration.
CMake/Windows execution is unverified. Analog model/profile unchanged.

Fresh0.22 original main2.1/scanner3.1/both PROMs40M CPU clocks: Insert Diskette,
7 DRAM writes/12 reads/95 WAIT holds,32 paired PIO transfers,100M PROM clocks
and34 final upper histories retained. Register SRAM consensus remains16/64
known cells; final sampled ALU known mask remains0, so this does not resolve
all whole-machine uncertainty. Log:
validation/alu-correlations-main21-scan31.txt. Historical0.14 ALU logs retained.


## Checkpoint0.23 — ALU joint-sample provenance

2026-10-18 Linux GCC13/C++20:18 native targets PASS,0 compiler warnings;
ALU ASan/UBSan PASS. Run `bash tools/build-native.sh`. Logs:
validation/tests-alu-joint-sample-suite.txt and sanitizer-alu-joint-sample.txt.

46656 partial cases compare exact known masks to independent concrete integer
completion:26244 independent-control plus20412 compatible jointly sampled
UCD04 cases. A new per-phase provenance bit startsfalse and is established
only AFTER both control latches capture the common PROM signal. It restricts
A4a/A4b to00/11 without choosing either value. Reset/PROM reload preserve it;
startup, disabled datapath and falling edges do not establish it. Old-edge
ALU arithmetic, latch timing and known-word results are retained.

A connected synthetic PROM uses uncertain PRECAR with known staged low20
bits and checks actual ALU capture knowledge. Removing only the controller's
relation argument in an isolated temporary source copy causes that regression
to fail as expected (validation/alu-joint-sample-negative.txt). This checks
actual integration, beyond component arithmetic or mere soundness.

Fresh original-ROM commands, unchanged source inputs:

```sh
build/eii_probe ../roms/main.bin ../roms/scanner.bin 40000000 /tmp/eii-alu-joint-sample.csv ../roms/ic135.bin ../roms/ic137.bin
build/eii_probe ../roms/diag.bin ../roms/scanner.bin 4000000 /tmp/eii-alu-joint-sample-diag.csv ../roms/ic135.bin ../roms/ic137.bin 'HE TE'
```

Main2.1/scanner3.1: Insert Diskette,7 DRAM writes/12 reads/95 WAIT holds,
32 paired PIO transfers,100M PROM clocks,34 upper histories and16/64 known
register cells. Final sampled ALU mask0 remains explicit. Diagnostic:
Static RAM OK/help,0 DRAM cycles/WAIT holds. Logs
validation/alu-joint-sample-main21-scan31.txt and
alu-joint-sample-diag-testhelp.txt. Previous checkpoint logs unchanged.

This refines one proven shared-sample relation, not complete operand/flag/
controller histories or physical clock skew/propagation calibration. No
Windows/CMake execution claim. Scanner ADC/matrix remains a placeholder;
service PDF71 wiring inventory is in SCANNER_PANEL_PLAN.md. Panel ADC0809
and channel0 audio SAR are separate future hardware paths.


## Checkpoint0.24 — scanner ADC0809, matrix and panel controls

2026-10-18 GCC13/C++20:19 native targets PASS,0 compiler warnings. Run
`bash tools/build-native.sh`; validation/tests-scanner-controls-suite.txt.
Scanner-controls ASan/UBSan PASS with ASAN_OPTIONS=detect_leaks=0 (environment
leak detection unavailable), validation/sanitizer-scanner-controls.txt.

New tests:2048 ADC channel/code conversions,765 below/on/above half-LSB
boundaries, full rail/invalid voltages, ALE/START hold/abort, live SAR input,
unknown data/address consensus, OE and output-one-clock-before-EOC. All256
PIO-B selectors compare to independent OR-gate/decimal-LS42 equations, with
all20x8 contacts and multiple pressed contacts. Named Select/Zero/Filter
follow physical connector wires, including the conflicting SBUS42/40 label
at CN3 pin8. LED latch release/aliases are tested independently of GUI.

Real scanner Z80 programs select/read all eight analog inputs through four
benign aliases, read the matrix via PIO-B mode3 direction/output pins, and
capture all three LED latches on OUT release. Another programs CTC3/IM2,
holds the completed ADC interrupt pending under DI, then reads code192 in
exactly one ISR and clears service through RETI. No fake IRQ callback.

Fresh original-ROM commands:

```sh
build/eii_probe ../roms/main.bin ../roms/scanner.bin 40000000 /tmp/eii-scanner-controls.csv ../roms/ic135.bin ../roms/ic137.bin
build/eii_probe ../roms/diag.bin ../roms/scanner.bin 4000000 /tmp/eii-scanner-controls-diag.csv ../roms/ic135.bin ../roms/ic137.bin 'HE TE'
```

Main2.1/scanner3.1 reaches Insert Diskette. ADC5M source clocks,2478 starts
and completed conversions,0 aborts/unresolved CPU reads/overlapping decoder
cycles,117 LED captures. Final ADC input2/result128 is known. Main retains
7 DRAM writes/12 reads/95 WAIT holds,32 paired PIO transfers,100M PROM clocks,
34 upper histories and16/64 known register cells. Final sampled ALU mask0
remains unknown. Scanner instruction counts and108 LCD writes differ from
old placeholder timing/input values; real main/scanner handoff still works.
Log: validation/scanner-controls-main21-scan31.txt.

Diagnostic4M HE TE retains Static RAM OK/help with0 DRAM cycles/WAIT holds;
ADC251 starts/completions,0 unknown reads/aborts/overlapping decoder cycles
and12 LED captures. Log: validation/scanner-controls-diag-testhelp.txt.

ADC64-clock/decision-phase/source-origin timing is an explicit nominal model,
not exact physical chip calibration. Unknown ADC bits become1 only at the
Z80 interface and are counted; no such reads occurred in these real-ROM
runs. Frontend networks/trim/capacitor settling, overlapping chip-selection
bus contention, physical contacts/pedals and audio volume/input-level pots
remain incomplete. See SCANNER_CONTROLS.md for source hashes and boundaries.
No CMake/Windows runtime, GUI, MIDI, OS disk boot or audio SAR claim.


## Checkpoint0.25 — floppy homing, INDEX/READY and SIO modem path

2026-10-18 Linux GCC13/C++20:20 native targets PASS,0 compiler warnings;
`bash tools/build-native.sh`, validation/tests-floppy-mechanics-suite.txt.
Floppy mechanical/SIO/IM2 tests ASan/UBSan PASS with detect_leaks=0,
validation/sanitizer-floppy-mechanics.txt. Final targeted native test and
probe CLI guards pass in tests-floppy-mechanics-final.txt and
tests-floppy-probe-cli.txt. After the full suite/sanitizer, only test-output
wording and probe input validation were changed; core/model remained identical.
CMake0.25 targets are added but CMake/Windows execution remains unverified.

Component tests home all80 explicit initial cylinders, enforce trailing-edge
STEP/direction/minimum spacing/power guard, and check outer-stop phaseA
TRACK00 toggling, nominal spindle/index/READY/stop delay and two-drive common
motor/selection wiring. Real synthetic Z80 homing from37 uses PIO mode3
read/write pulses, not direct host head movement. SIO modem-latch/vector/
reset tests and real IM2 SIO>CTC>PIO ordering/own RETI tests cover pending
CTC restoration across upstream SIO service without persistent INT leakage.

Original firmware mechanical experiment:

```sh
build/floppy_probe ../roms/main.bin ../roms/scanner.bin ../roms/ic135.bin ../roms/ic137.bin 4000000 37 /tmp/eii-floppy-homing.csv
```

Explicit SA465-reference drive0 at cylinder37/phase400000; drive1 absent;
read-only SENSOR-ONLY medium with no sectors, bitstream or OS bytes. In4M
CPU clocks original main2.1/scanner3.1/both PROMs execute37 outward steps,
reach0/phaseA/TRACK00 active, with0 early steps/timing violations. Three
steady index pulses and three real SIO-A acknowledgements occur; firmware
executes its index handler02F3, ready handler0311 and data-read setup031E.
It then waits for the unfinished serial-data/DMA transport. This is NOT
an OS boot. Strict CPU DRAM remains7 writes/12 reads/95 WAIT holds.
Evidence: validation/floppy-homing-main21-4m.txt and the12 retained actual
PIO/SIO observations in floppy-homing-events.json.

No-drive regression:

```sh
build/eii_probe ../roms/main.bin ../roms/scanner.bin 40000000 /tmp/eii-floppy-no-drive.csv ../roms/ic135.bin ../roms/ic137.bin
build/eii_probe ../roms/diag.bin ../roms/scanner.bin 4000000 /tmp/eii-floppy-diag.csv ../roms/ic135.bin ../roms/ic137.bin 'HE TE'
```

Fresh40M: Insert Diskette,32 paired PIO transfers,7 DRAM writes/12 reads/
95 WAIT holds,100M PROM clocks,34 upper histories and16/64 known register
cells, final ALU mask0 unknown. Both drives absent;0 SIO transitions/ACK or
unsupported operations. Diagnostic4M HE TE retains Static RAM OK/help and
0 DRAM/WAIT/SIO events. Logs floppy-no-drive-main21-40m.txt and
floppy-diag-testhelp.txt.

Reference-profile limits and source hashes are in FLOPPY_MECHANICS.md:
explicit known cylinder/rotor initial conditions, nominal guards/fixed-rate
spindle/2ms pulse, ideal logical TRACK00 distinct from15ms data settling;
no measured physical motion/propagation/acceleration calibration or complete
arbitrary cold-start envelope. SIO is a modem-status/control/interrupt subset;
RX/TX/sync/CRC, external read separator, DMA and actual image transport remain
unfinished. No ROM patch, RAM sector injection or fabricated data-ready flag.


## Checkpoint0.26 EMUIIFD/FM and read-separator laboratory

`bash tools/build-native.sh`:21 native targets PASS,0 compiler warnings
(`validation/tests-floppy-image-suite.txt`). No emulator core/runtime changes;
existing scanner/controller/analog/mechanics/IM2 tests retained. No fresh40M
original-ROM run:0.25 firmware evidence remains historical and unchanged.
CMake0.26/Windows builds and runtime were not executed.

New test validates160 synthetic track/sides x2 explicit gap profiles, every
serialized payload bit, independent MSB-first polynomial CRC, all256 one-byte
inputs and CRC-16/ARC123456789=BB3D, frame offset/size,13-bit phase search and
revolution wrap. Deliberately corrupted payload/header CRC, missing FM clock,
invalid slot/input-size/profile/cylinder/side/time/expiry overflow are rejected.
LS221/LS74 nominal per-edge separation recovers every bit for65/80/87/100/127
source-edge widths;63/129-edge negative controls detect failed recovery.
ASan/UBSan PASS with `ASAN_OPTIONS=detect_leaks=0` (environment limitation),
`validation/sanitizer-floppy-image.txt`. CLI argument/file/profile/size guards
PASS in `validation/tests-disk-image-cli.txt`.

`disk_image_probe ../inputs/zm325.emuiifd report2010` and `hxc2026` each
verify all160 original-payload mappings, generated framing/CRCs and31250
read-separator bits per revolution. Metadata-only JSONs:
`validation/floppy-zm325-report2010.json` and `floppy-zm325-hxc2026.json`;
provenance/initial conditions in `floppy-format-provenance.json`. Both JSONs
parsed and compared: all IDs/offsets/CRC summaries match; marker difference
exactly4bits. Source hashes, visual verification and limits: FLOPPY_FORMAT.md.

These CRCs/flux/gaps are synthesized from a payload-only image. They do not
validate original media CRC or recover actual gap/index/flux history. The
87-edge LS221 width/8-edge pulse offset/known idle startup are nominal lab
conditions; no physical calibration. Machine has no image mount or transport;
SIO receive/delayed CRC/READY and DMA/OS boot remain future work.


## Checkpoint0.27 programmed SIO receive / byte-mode DMA

Native `bash tools/build-native.sh`:23 targets PASS,0 compiler warnings
(`validation/tests-sio-dma-suite.txt`). New tests exercise actual per-bit
Bisync synchronization,4-clock raw-bit/FIFO and20-clock CRC delay, corrupted
CRC,3-byte FIFO/overrun/status/aliases/Ready, first-character IRQ rearm and
RX-before-ext nested vector/RETI. FM+separator+SIO and rotor/head/side/100us
guard/deselection fixtures use pin clocks; real Z80 OUT/IN receives/stores
its byte. Unsupported formats consume no fabricated data.

DMA tests verify programmed follow bytes, Ready polarity, N+1 and zero65537
counts, address wrap/Continue, per-cycle pins/WAIT and EOB/vector/RETI. Real
Z80 is frozen under bus ownership and handles one real IM2 EOB interrupt.
All8 independent DMA banks write4 known bytes through strict RAS/CAS/WAIT
consensus across16 startup paths, exactly4 commits with DMA wait clocks;
CPU counters remain separate and scanner/controller clocks keep running.
No decoded-sector RAM block copy is used.

After the full suite, final targeted native/SIO and ASan/UBSan checks verify
Hunt-register retention, DCD AutoEnable/FIFO retention and the conservative
uninitialized-CRC boundary. Logs tests-sio-final.txt, sanitizer-sio-receive-
final.txt and sanitizer-disk-dma-final.txt. Earlier sanitizer logs are also
retained; final logs are authoritative for these corrections. Leak detector
disabled (`ASAN_OPTIONS=detect_leaks=0`) as required by the environment.

Fresh original-ROM tests: both nominal image profiles at3.6M clocks transfer
4header +3584payload bytes,3584 strict DMA DRAM writes/17920 DMA wait clocks,
2DMA ACKs and3SIO-A ACKs. All3584 bytes at2800..35ff compare known/equal with
original ZM325 data; captured source-bus bytes independently match the image.
Header RR1=01 is accepted, then payload-end RR1=41 causes a real firmware
CRC-retry branch. Evidence sio-dma-zm325-hxc-3m6.txt, sio-dma-zm325-report-3m6.txt
and sio-dma-first-block.json. Private raw data traces stay outside main.

Fresh no-drive main2.1/scanner3.1/both PROMs40M still reaches Insert Diskette:
7CPU DRAM writes/12reads/95WAIT,32paired PIO transfers,100M PROM clocks,
34upper histories/16known RF cells;0SIO transitions/ACK/unsupported operations,
0DMA transfers. Diagnostic4M HE TE retains Static RAM OK/help and0DRAM/WAIT/
DMA. Logs sio-dma-no-drive-main21-40m.txt / sio-dma-diag-testhelp.txt. These two
regressions preceded the final uninitialized-CRC guard/report additions, which
do not affect their executed receive paths. Final media runs and targeted
receiver checks include that guard. No fresh Windows/CMake execution claim.

The current SIO CRC accumulator/Hunt transition still fails a valid payload
check. No CRC-success override, guessed reset borrowed from another chip,
firmware patch, direct RAM image injection or accepted disk/OS boot claim.
The bus adapter is conservative and Ready/pulse/reset timing is nominal;
full serial/DMA modes, unrelated Ready sources and physical BUSACK/subcycle
calibration remain absent. Full source/model limits are in SIO_DMA.md.

## Checkpoint0.28 Hunt retention and independent CRC audit

`bash tools/build-native.sh` freshly builds/runs all23 native targets, PASS
with0 compiler warnings (`tests-sio-hunt-suite.txt`). New receive assertions
cover all15 FA96 split positions with repeated Hunt, distinct disable/re-enable
clearing and AAAA overlap reacquisition two clocks after immediate Hunt.
An isolated0.27 receiver substitution fails that regression as intended
(`sio-hunt-negative.txt`). Final SIO ASan/UBSan, leak detection disabled,
passes after all new assertions (`sanitizer-sio-hunt.txt`). No sanitizer
diagnostics. CMake was attempted but its executable is unavailable (exit127);
neither CMake execution nor Windows build/runtime is claimed.

Fresh original main2.1/scanner3.1/PROMs3.6M-clock passive probes under both
profiles independently divide actual RxD bits using MSB8005 and compare
against the LSB receiver at the documented20-clock delay. Header34 and
payload28709HxC/28708report clock checks have0 mismatches. The XOR between
carried-seed and zero-seed shadows equals the independently shifted seed
influence at every checked clock. Each shadow is diagnostic only.
See sio-crc-audit-hxc.txt / sio-crc-audit-report.txt and SIO_CRC_AUDIT.md.

At the actual payload RR1read, CRCB04A equals the carried5000hex seed's
influence, while the zero-seed shadow is0000. Thus initialization accounts
for the observed error within this model; actual silicon behavior remains
unproven and the accumulator-preserving policy is unchanged. No guessed
reset/CRC override/firmware modification/direct disk-to-RAM copy is applied.
Both probes independently compare3584 raw pin-derived payload bytes and
3584 known strict DRAM bytes with the image;3588DMAtransfers/3584writes,
CPU7writes12reads95WAIT and Boot in Progress are retained. Real firmware
still retries; no accepted diagnostic/system disk boot or audio.

Historical0.27 no-drive40M and diagnostic4M evidence is retained without a
new run; those paths do not exercise the changed receive clocks/Hunt logic.
The source-only correction and passive observer leave the CRC reset policy,
bus adapter, firmware, nominal formatting and all analog kernels unchanged.

Audit negative control: changing only the isolated tool observer delay from20
to19 clocks produces28710 payload comparison mismatches while actual raw
payload/RAM remain3584/3584 equal. Expected FAIL is recorded with an explicit
negative-control PASS in sio-crc-audit-negative.txt; no working source changed.

## Checkpoint0.29 isolated Hunt-reset hypothesis, not default boot

No core/vendor change. New tools/sio_hunt_lab.py and its laboratory-only
C++ harness copy sources to a temporary directory and compile either the
unchanged receiver or an explicitly unverified reset-on-Hunt assumption.
All input assets must match the identified fixture hashes. JSON logs include
core/vendor/harness hashes, isolated mutation hashes and source/asset
immutability checks. No raw payload or data-bearing trace is saved.

Four fresh4M-clock original-ROM/image trials pass their expected results:
sio-hunt-lab-retain.json, sio-hunt-lab-reset-hxc.json,
sio-hunt-lab-reset-report.json and sio-hunt-lab-corrupt.json. See
SIO_HUNT_LAB.md for commands and fixed nominal initial conditions.

The clean reset-hypothesis trials accept the first payload with RR1=01/CRC0,
all3584RAM bytes known/equal at the CRC read before loaded code executes.
They subsequently execute29033(HxC)/29026(report) instruction fetches from
the loaded2800..35ff region,40main-to-scanner PIO transfers and actual firmware
LCD Testing Memory / Bank=A Segmnt=1. CPU strict DRAM W/R/WAIT at4M is
8295/33268/207819(HxC) or8293/33260/207769(report). No complete RAM pass.

The unchanged retain control retries with RR1=41/CRCB04A and0loaded-code
fetches despite byte-exact payload. The reset-hypothesis negative control
flips exactly one FM data slot while preserving computed CRC/clock slots:
RR1=41/CRC3F1A,3584known/3583equalRAM and0loaded-code fetches. Thus the
hypothesized reset does not force success on corrupted input. At4M both
retrying runs have started a subsequent read; use the first payload event
for complete-block comparisons rather than the larger cumulative counters.

Launcher syntax/unique mutation anchors and rejection of wrong asset hashes
before compile/run pass, as do existing native SIO/DMA targets
(tests-sio-hunt-lab-tools.txt).0.28 full23-target/sanitizer evidence retained;
no new full-suite/sanitizer/CMake/Windows execution claim for the lab tools.
Default receiver still preserves CRC across Hunt. Conditional execution is
not evidence of actual chip reset behavior or a verified/default OS boot.


## Checkpoint0.32: OS3.0 unwritten-DRAM read and explicit startup state

See DRAM_POWER_ON.md and validation/os30-dram-root-cause.json. Independent
unknown-startup run verifies all28672 OS bytes at2800..97ff equal disk prefix,
then blocks at43c8 reading98c0 (visible PC43c9). Bank/MIDI shutdown runs before
that parameter is initialized. The default uncertainty model is retained.
Two nonzero xorshift32 power-on patterns pass the read and reach55558DMA at24M
with firmware LCD This Will Take / A While; this is not complete loading.
The inline startup loop was extracted into initializeDramPowerOn(seed); final
API13M seed1 case advances to5410CPU DRAM reads, while final unknown-control
stops at12436965ticks/3542reads/28720DMA.
Focused native power-on/DRAM/DMA/SIO checks pass; no new full-suite, Windows,
CMake, sanitizer or physical calibration claim. PR1's CRC implementation/tests
are selectively adopted, its three defective helper additions remain excluded.
No ROM, disk, full RAM snapshot or disassembly is committed.


## Checkpoint0.33: real OS/null-bank readiness and panel/ADC response

See OS30_BOOT.md and validation/os30-boot-provenance.json. Pure phase0/seed1
and phase15/seed305419896 trials reach P01 NULL PRESET by178.9M ticks. The
audited145payloads all pass actual RR1 and byte comparison:519680bus bytes,
518144DRAM bytes,1536ROM-overlay discards,514048unique DRAM locations. At200M
normal idle/interrupt activity continues with24known CV holds; no held read.
Phase0 panel trial closes/releases Filter raw33/5, reloads OS sectors5/6,
prints No Vce in Preset on row2; sliderA2.5V is read as ADC128 on111scanner
CPU reads. Its147CRC checks and526848payload bytes all match.
All16-trajectory reference reaches80M/224286DMA without stall, not full ready.
Observer-only one-bit expected-value negative control detects1bus/1RAM/1final
mismatch (exit1), while actual CRC/firmware still pass;4M all-phase positive
control reports no byte/CRC errors. Final CLI/default/phase guards pass;
second-row-only response replay catches/corrects the draft first-row flag.
The emulator core is unchanged from0.32. No new broad native suite, Windows,
CMake or sanitizer run; no sound, host MIDI, physical timing or arbitrary
startup/media compatibility claim.

## 0.58 quiet-edge differential checks

See PRODUCTION_IDLE_058.md for exact scope and unresolved rapid-note hang.
Standalone tests use synthetic PROMs and memory, with no private assets:

```sh
g++ -std=c++20 -O2 -DEII_DIAGNOSTICS=1 -Isrc tests/production_dram_idle_tests.cpp -o /tmp/eii-dram-idle
/tmp/eii-dram-idle
g++ -std=c++20 -O2 -DEII_DIAGNOSTICS=1 -Isrc tests/production_quiet_tests.cpp -o /tmp/eii-controller-quiet
/tmp/eii-controller-quiet
```

Add `-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer` for the native
sanitizer checks. This environment requires `ASAN_OPTIONS=detect_leaks=0`.
CMake test targets were registered; CMake itself was unavailable locally.

## 0.67 host report-flow audit

See [SIGNAL_FLOW_AUDIT_067.md](SIGNAL_FLOW_AUDIT_067.md) and
`validation/report-flow-067.txt`. Real synthetic Z80 serial flood exposes
history-dependent release GUI work; default view is now quiet and explicit
live serial is bounded. Manual capture retains the complete existing report.
No core, filter or host audio timing change; native Win64 execution remains
unverified here.

## Filter restart investigation after0.67

Supplied Marcato HFE validates160 sectors and boots with real firmware.
Ten-second exact-pin replay: continuous vs restart, two repetitions each,
96k internal rendering and supplied profile. After settling, solver iterations
13,846,334 vs13,846,336 for6,144,000 samples per group; zero fallbacks, held or
nonfinite outputs. No persistent solver-work decrease demonstrated; Windows
thread/queue attribution remains open. See FILTER_RESTART_AUDIT.md. No new
production runtime change or executable in this investigation.

## 0.68 optional performance recording

Record trace/Stop+Save available on vector panel and Settings; UI-owned JSONL
ring with250ms samples,1Hz thread/host context and requested panel/model events.
Full digital-report equality off/on verified by synthetic Z80 session test.
Seven targeted native suites plus independent JSON parser PASS. Win64 release
cross-build PASS with Werror. Windows GUI/save-dialog runtime not tested here.
See PERFORMANCE_TRACE_068.md and validation/performance-trace-068.txt.
