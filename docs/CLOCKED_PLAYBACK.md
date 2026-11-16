# Clocked playback and capture measurement — checkpoint0.20

Machine now has an opt-in continuous nominal playback experiment. It consumes
actual stored DAC codes, amplitude-CV holds, HC393 phases and S3528 program
words at every20MHz half-edge. This connects the existing kernels to Machine
time. It is not calibrated hardware audio, a real-time host backend or OS
sample playback.

## Ordering and interval model

`ChannelTimers::advanceCpuTickObserved` calls an observer before each of the
five source half-edges in one4MHz CPU tick. The observer sees settled state
for the preceding50ns interval; normal controller rising/timer falling edges
then execute in their existing order. CPU/scanner/PIO operations remain at
CPU boundaries. The non-observed path uses the same loop with an empty inline
observer. Enabling playback does not feed analog values back into the CPUs,
DRAM, request/ACK logic or controller histories.

Each enabled channel runs reference/I-V, phase-resolved deglitch, zero-Q SSM,
explicit S3528 nominal reconstruction profile, C93/VCA and feedback kernels. The previous
DAC grid solution is held through the deglitch interval; the reconstruction
output held before a sample event feeds the current VCA grid update. These
50ns zero-order holds are stated numerical approximations, not exact coupled
continuous-time circuit integration. Physical switch/op-amp delays, clipping,
nonlinear controls, S3528 input amplifier/loading and calibrated chip response
remain outside the model. See SWITCHED_ANALOG.md and PLAYBACK_ANALOG.md.

S3528 master half-edges occur every other20MHz grid interval, preserving the
Machine's absolute source phase across arbitrary run() block sizes. Each
channel has its own divider. Divisor changes/unknown code invalidate phase and
reconstruction immediately, including between master edges. A stale known
output cannot survive by waiting for the next internal clock event. Phase is
not automatically reset or chosen after programming.

## Explicit initial conditions and controls

`Machine::enableClockedPlayback(options, sink)` attaches an engine at the
current absolute half-edge; `disableClockedPlayback()` detaches it. Options
contain output rate, active channel mask, explicit pin-domain gm and VCA branch
current, and channel0 playback confirmation. No invented mapping from board
frequency/Q/mixer voltages is supplied. Defaults preserve unknown capacitors,
controls, channel0 MODE and divider phase.

For laboratory comparisons only, `zeroCapacitorsForExperiment` and
`alignDividersForExperiment` establish chosen initial conditions. Alignment
applies only on the first interval and only for already-known program words;
it does not wait for a future code then pretend firmware reset a divider.
Re-enabling with experiment flags is a new chosen experiment, not physical
state recovery. Unknown histories stay unknown without explicit reinitialization.

An ideal deglitch result reaching/exceeding the schematic's +/-15V supplies
sets a sticky per-channel `rangeExceededMask` and makes downstream output
unknown. This rejects a physically impossible linear solution; it supplies no
rail clipping law and does not establish the actual smaller linear swing.
Results inside the supply bounds are not thereby certified linear/calibrated.

The frame callback must only consume the frame; do not mutate or re-enter the
Machine from it. Sink exceptions abort the capture. No real-time performance,
thread-safe callback or audio-device API is provided by this checkpoint.

## Rational output frames

`PlaybackFrameIntegrator` integrates the piecewise constant grid output over
exact rational frame boundaries.44100,48000 and96000Hz do not require rounding
20MHz into an integer decimation factor. It accumulates overlap weights as
integers and divides by20000000. No phase is reset between run() calls. A
frame's index starts at1 and its end time relative to capture is index/rate.
Incomplete final frames are retained internally, without padding or flushing.

Any unknown portion makes that channel's entire overlapping frame unknown;
later frames can become known when all their contributing intervals are known.
Disabled channels remain unknown. Unknown samples are never written as known
silence. Frames carry eight separate channel voltages, not an invented mixer.

This box integration has sinc droop and weak stop-band rejection. It is a
measurement/observation output, NOT a production anti-aliasing resampler.
Do not use it to claim calibrated audible reconstruction or a finished audio
backend. A characterized anti-alias filter and host-output path remain next work.

## CSV capture tool

Build with `bash tools/build-native.sh`. Usage:

```
build/audio_probe main.bin scanner.bin ic135.bin ic137.bin WARMUP_CPU_CLOCKS CAPTURE_CPU_CLOCKS output.csv 48000
```

Optional explicit laboratory flags:

```
--zero-caps-experiment --align-dividers-experiment --gm=0.0001 --vca=0.00001 --confirm-channel0-playback
```

The flags are recorded in CSV metadata; gm/current values apply to all channels.
The file records frame/time/known_mask/eight voltage columns. Unknown voltages
are blank. Standard output contains the firmware report plus interval/frame/
known-frame and supply-exceeded counters. The tool does not change ROMs or
insert sample data. An ordinary boot without loaded OS/sample/control states
correctly produces unknown observations. CSV has no audio full-scale mapping.

## Measuring an actual sine capture

`tools/measure_tone.py` analyzes a supplied simultaneous input/output capture:

```
time_s,input_volts,output_volts
```

Use defined, documented stimulus and observation nodes. A whole-channel test
cannot identify S3528 alone while deglitch/SSM/control behavior is unresolved.
An isolated S3528 fixture must account for its input op-amp and loading; this
checkpoint does not prescribe an unverified probing/bypass connection. Keep
amplitude small, record supply/clock/program/loading/temperature and repeat
measurements before fitting any chip parameters. Input/output must share a
time reference; uncharacterized acquisition-channel delay contaminates phase.

Example command after such a capture exists:

```
python tools/measure_tone.py capture.csv --frequency 1000 --kind hardware --fixture documented_fixture_name --program-code 3B --master-hz 5000000 --skip-seconds 0.02
```

NumPy is required for this offline analyzer only. It fits cosine/sine/DC jointly
for both channels, reporting tone gain, relative phase, DC and residual RMS.
The requested frequency is supplied, not estimated; use the actual stimulus
frequency. A mismatched frequency or unsettled/drifting record can bias the
fit. Discard startup with skip-seconds. At least ten retained cycles, increasing
finite timestamps, resolved input/output tone and a frequency below Nyquist
at the largest timestamp gap are required. Residual RMS includes noise,
harmonics, drift and model mismatch; it is not an isolated THD/noise metric.

JSON records hardware versus simulation provenance and always says
`unvalidated_capture_measurement`. Merely labeling a capture hardware does
not validate its fixture or calibrate the emulator. No actual EII hardware
capture has been supplied or used in0.20. The rejected AI coefficient list is
not measurement data; its DC-normalized passband drops about8.02dB by its
claimed cutoff, inconsistent with its stated0.1dB target.

## Executed validation

- Native17-target suite, including the existing16 regressions.
- Independent frame-overlap reference at44100/48000/96000Hz, exact counts,
  unknown spans, constant/ramp amplitudes and invalid-rate guards.
- Real Z80 OUT staging->RF->ALU->DRAM->eight DACs, plus amplitude holds/timers/
  S3528 programming, drives a10ms eight-channel continuous capture:480 frames.
  Experiment controls/zero caps/phase are explicit; direct bypasses stay off.
- One-block versus400x100-CPU-clock execution gives bit-identical frames;
  detached/attached/chunked CPU/PROM/WAIT/DRAM/timer reports match exactly.
- Selected-channel reprogramming loses phase/output certainty without affecting
  another channel. Unknown startup remains unknown. Impossible ideal output
  beyond supply limits is rejected.
- Offline tone analyzer matches independently constructed997Hz input with
  amplitude0.25 transfer,123us delay, DC offsets and a known second harmonic.
  Invalid/short/reversed/nonfinite/no-input captures are rejected. This tests
  measurement arithmetic, not physical hardware calibration.

Validation logs and original-firmware regression details are in VALIDATION.md.
CMake and Windows runtime are not executed. No real-time, OS-boot, MIDI or
calibrated hardware-sound claim is made.

## Checkpoint0.21 reconstruction profile

The default is now ami1993_typical_curve_fit, documented in S3528_CURVE_FIT.md.
Capture metadata identifies it; --legacy-generic-reconstruction selects the
original generic_elliptic profile. The timing/capture/unknown-state conventions
above are unchanged. This profile change does not provide physical calibration.
