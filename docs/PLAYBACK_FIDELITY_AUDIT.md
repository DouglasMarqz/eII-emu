# Playback timing and final PCM audit, 2026-10-21

Baseline:0.69 (`d804691ab0caa3615b4f1b8825e63d05d6b74972`). This checkpoint
adds an offline measurement facility and regression tests. It does not change
the released audible engine or claim a CPU improvement. The next production
change needs an event-preserving analog input bridge, rather than a global
replacement of48k with27.777k.

## Visual schematic check

The original service manual remains outsidegit, SHA256
`f1071d347818ecfd107df0808ceade6d9e460987a85f86a04c71707a82700031`.
Direct image inspection was used for these connections, rather than OCR
inference. Existing OCR was only a locator.

| Sheet / PDF page | Rechecked path |
| --- | --- |
| 7-40 /102 | IC1..3 8254-2 CLK inputs share +C10M.D; gates tied high |
| 7-41 /103 | IC27 HCT138 rotated ACK wiring: Y0->CHAK7, Y1->CHAK0, etc. |
| 7-45 /107 | CHDB->IC48 HCT273, clocked by electrical -CHAK1 rising; Am6072/I-V follows |
| 7-45 /107 | IC41 HC393 and Q0/Q1 decode generate separate13202 phases; C74/C83=470pF, R26=200ohm, R34=10kohm |
| 7-45 /107 | SSM2045 four-pole output->S3528->C93/coupling->VCA->R154/C161 output feedback |

The same channel-phase structure was checked in the channel2..7 image
contact sheet. Channel0 has the separately documented sampling-mode decode;
do not substitute channel1's full decoder for it. See CHANNEL_REQUESTS.md,
SAMPLE_TRANSPORT.md and SWITCHED_ANALOG.md for the earlier pin-level work.

## What already runs at original timing

The digital scheduler has a nominal10MHz clock and20MHz half-edge grid:
five50ns intervals per4MHz CPU tick. The real firmware programs the8254
reloads, the controller reads sample DRAM, IC40 captures CHDB, and rising
rotated ACK transfers data into the individual DAC registers. These operations
are independent of the host48k audio snapshots.

The boot-ROM360 divisor gives the rational nominal frequency
`10000000/360 = 250000/9 Hz`, or27,777.777...Hz. In the actual bank/Play
observation below, channel0 ACK releases were720 half-edges apart, exactly36us
within the digital model. This is not a measurement of a physical crystal,
nor a claim that all eight voices or the unimplemented recording ADC always
operate at that rate. Other channel reloads change under firmware control.

The audible path presently takes DAC/CV snapshots at CPU boundaries using a
rational48k accumulator. The nonlinear worker repeats each input for two/four
96/192k solver steps, then FIR-decimates to48k. This does not retain the exact
intra-frame DAC transition time or run the switched deglitch state. Modern
solver/output rates are legitimate; the missing event information is the
specific fidelity limitation. No blanket claim about corrupted notes or
automatic CPU savings follows from it.

## Offline observer and isolation

`EII_PLAYBACK_TIMING_LAB` exposes a read-only callback before each source
half-edge, describing the preceding50ns held interval. It reuses the complete
edge scheduler; it is deliberately too intrusive to use as a real-time speed
measurement. Callbacks must not mutate/re-enter Machine. Attach/detach and
block-size invariance are tested against full digital reports and DRAM for
both reference and production engines.

The member, API and callback branch compile out of ordinary builds. CMake's
`EII_BUILD_TIMING_LAB=ON` builds a separate core for the probe/tests, leaving
the GUI core unchanged. The native script uses the corresponding environment
switch; the Windows script uses `-TimingLab` and keeps the GUI macro off.
The final release crossbuild's entire .text section is byte-identical to the
accepted0.69 executable (1,106,432 bytes, SHA256
`fe9739f8f289c4f8be4f4e9c77dd93957eaa6ca166b7c6589e6977355ef67098`).
No replacement GUI release is needed for this measurement checkpoint.

## Real firmware timing observations

The probe boots the private Marcato bank with the established concrete seeds,
reaches P01 at183M ticks /520552 DMA transfers, and presses the actual Play
contact. LCD reaches S01 La Primavera /90BPM. After capturing eight seconds of
renderer inputs, it observes100ms at50ns resolution:2,000,000 intervals.

All eight DAC masks and both phase levels remain known; no phase overlap or
DAC ACK release during either switch's active phase occurs. Every measured
phi1-rise to phi2-rise interval is64 half-edges =3.2us. Channel0 holds a
constant360 reload; its ACK-to-next-phi1 delay is527 half-edges =26.35us.
Other channels vary in this window; their ranges must not be treated as
fixed per-note frequencies or unmeasured physical propagation delays.

| Channel | Final reload | ACK releases in100ms | Changed DAC values | Frames whose held-code area differs from endpoint pickup |
| --- | ---: | ---: | ---: | ---: |
| 0 |360|2778|2584|2584/4800|
| 1 |428|2337|2272|2272/4800|
| 2 |381|2625|2552|2552/4800|
| 3 |286|3497|3415|3415/4800|
| 4 |286|3496|3328|3328/4800|
| 5 |339|2950|2912|2912/4800|
| 6 |340|2941|2870|2870/4800|
| 7 |428|2337|2287|2287/4800|

This diagnostic integrates integer Am6072 signed code units over the exact
held intervals, then compares to the endpoint code times that frame's actual
duration. It isolates transition-time loss; it is not a reference analog
waveform, calibrated voltage, anti-aliasing resampler or THD measurement.
There is at most one changed code per48k interval in this stimulus: it does
not demonstrate complete omission of intermediate sample values. It does
demonstrate that endpoint pickup changes the duration of held input values.

A second100ms window follows Stop, CC123 and MIDI notes84..91. All27 MIDI
bytes are received/read with zero UART overrun; production remains valid.
All masks/phases remain known with zero overlaps/ACK-during-phase, and the
3.2us phi1-to-phi2 spacing remains. ACK releases range2728..3150 per channel
in that window, with final reloads286..360; do not infer a universal output
rate merely from the MIDI note numbers or the advertised recording rate.

## S3528: final PCM, not just internal coefficients

The same38400048k input packets from the real firmware are replayed through
`RuntimeAudioEngine -> ChipAudioWorker -> final stereo AudioQueue`, including
the normal decimator, DC blocker, float conversion and output clamp. Both
channels are finite and equal. The tape hash, including actual S3528 codes,
is `5e8282446464fdaf`. Every S3528 code has all six bits known.

Each mode runs bypass, repeat bypass and enabled from the same initial state.
The offline harness waits outside firmware for each96-frame DSP block and
drains the same bounded1056-frame queue, so overload cannot manufacture a
difference. Host dropped/missing, DSP dropped/failures and solver fallbacks/
held are all zero. Repeat bypass has zero differing float bit patterns.

| DSP rate | Bypass PCM hash | Enabled PCM hash | Changed left/right frame pairs | RMS OFF / ON |
| --- | --- | --- | ---: | --- |
|96k|`6dfe6b4665fd5a47`|`1130ce8872beebbf`|227559/384000|.0854349207 / .0859119449|
|192k|`cac8b2801bd217b0`|`ff302263930fd20e`|227559/384000|.0854444174 / .0859534242|

Hashes are the probe's FNV-1a over little-endian left float32 bytes, not
cryptographic artifact hashes. Stereo equality is checked independently.
RMS of the sample-by-sample difference is.0531449943/.0551800921; its peak
is.346072216/.355036696. Those differences include filter phase/delay and do
not imply equivalent loudness or distortion. Overall RMS changes by only
about+.0484/+.0516dB. This proves the stage affects the measured PCM path;
it does not establish audibility, hardware calibration, or the active state
of an unobserved Windows GUI session.

`s3528_pcm_tests` also exercises synthetic deterministic6000-frame stimuli
through the full transport: Legacy ON/OFF stays bit-identical; each nonlinear
mode changes5999 frames. All repeats match, with no missing/dropped frames.

## Validation and reproduction

Native GCC13.3 O3/NDEBUG/diagnostics0: observer reference/production/chunked/
detach tests, PCM transport test, clocked playback, sample transport,
production span runtime, and trace/session tests pass. Win64 release and
laboratory probe cross-compilation pass with LLVM-MinGW20261006 UCRT x86_64,
Wall/Wextra/Werror. Windows execution, device scheduling and listening are
not claimed. CMake itself is unavailable on this host; native targeted builds
and shell syntax were executed directly.

```
EII_BUILD_TIMING_LAB=1 bash tools/build-native.sh
build/playback_fidelity_probe ROM_FOLDER PRIVATE_BANK.hfe PRIVATE_CALIBRATION.txt
```

The long probe requires caller-owned ROM/media outsidegit; it emits aggregate
metrics only, not sample/audio files. The source-only repository contains no
ROM, disk, manual scan, captured sample tape or user calibration file.

## Next production boundary

Preserve timestamped DAC latch changes, switch-phase transitions, MODE and
CV/control changes in immutable bounded packets. Resolve deglitch charge/
hold behavior over the real intervals before feeding the nonlinear VCF and
S3528/VCA. Build this from certified events rather than running the entire
analog solver at20MHz or installing the offline observer in the GUI. Retain
the accepted path for controlled comparison; verify block invariance,
known-state policy, PCM spectra/transients and digital state before changing
the default. This audit does not yet implement that new audible bridge.
