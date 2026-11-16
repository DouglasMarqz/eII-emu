# 0.71: reconstruction diagnostics and service controls

This is a diagnostic/calibration checkpoint. It does **not** establish that the
native DAC/deglitch route reproduces an actual Emulator II's frequency response.
Native DAC remains opt-in and OFF by default. No default gain compensation or
arbitrary equalizer has been added to disguise the reported dark/quiet sound.
The stable branch and the reference route are retained.

## Fixed: Legacy checkbox requested a stage that was never rendered

The source audit found a concrete missing route: `reconstructionEnabled()`
required a nonlinear worker, and `BasicPlaybackVoice` unconditionally bypassed
S3528. Checking the box in Legacy mode therefore had no audio effect. This
predates native DAC; native DAC cannot explain that original omission.

0.71 inserts S3528 after Legacy VCF and before C93/VCA, and supplies the actual
firmware codes from Machine. Legacy holds its provisional48k VCF output for two
96k S3528 steps and averages the two outputs. This optional audio-rendering
approximation does not change the emulated sample clocks. It is not an analog
clock-image model or a complete high-rate Legacy redesign. Bypass remains bit
exact against a frozen0.70 Legacy voice. The user has not confirmed which mode
was active during every earlier S3528 report; this fix does not prove the same
cause for an SSM-mode report.

The checkbox now displays **ACTIVE**, **bypass**, or **requested / not active**
from the returned engine snapshot. Command-queue tests cover ON/OFF in Legacy,
ON in SSM and retained ON when switching back to Legacy without advancing or
resetting firmware. Final PCM tests cover the stage reaching all three modes.
A native Windows mouse-click test remains pending.

## User observations and what is established

The user reports strong low-pass behavior when enabling native DAC/deglitch, and
no audible S3528 change, including on builds predating native DAC. These are two
separate observations. Do not infer that native DAC explains the earlier S3528
observation.

`tools/native_response_probe.cpp` isolates the existing static reference, native
DAC/deglitch, and S3528 at a fixed 27,777.777... code updates/second and 96k
renderer. It feeds **raw companded sine codes, not differential-encoded audio**.
The source waveform is quantized identically on all four routes; reported gain
is normalized to the reference fundamental at each frequency. There is no
SSM2045/VCA, host gain trim or normalization in this isolation experiment.
Results: [CSV](../validation/native-response-071.csv).

| Frequency | Native vs reference | S3528 alone (code35) | Both vs reference |
|---|---:|---:|---:|
| 100 Hz | -3.63 dB | +0.002 dB | -3.62 dB |
| 1 kHz | -21.17 dB | +0.157 dB | -21.01 dB |
| 3 kHz | -30.54 dB | +0.164 dB | -30.37 dB |
| 6 kHz | -36.06 dB | -52.34 dB | -88.39 dB |

Thus the implemented native network really is strongly frequency dependent.
The S3528 kernel has a substantial measured effect, but a kernel test alone
cannot verify a Windows checkbox or explain a user's listening result.

The service schematic (7-45) has R26=200ohm, R34=10kohm and C74=C83=470pF.
Its modeled fully settled charge-transfer recurrence is approximately
`out[n]=50/51*out[n-1]-50/51*iv[n]`: a leaky integrator, with roughly87.5Hz
corner at this code rate. Differential sample reconstruction is a live lead:
[the Translator format implementer's documentation](https://www.chickensys.com/translator/documentation/formatinfo/emu.html)
describes differential EII sample storage. This does **not** validate our exact
recurrence, phase polarity, gain, or a particular bank. Do not remove the
integrator or claim calibration is solved on that basis.

A real original-firmware boot with a caller-supplied piano bank selected
S3528 code59, known mask63, on all8 channels: **12,500Hz**, not the3205Hz
isolation setting. That may make its audible difference small; it does not
prove the user's earlier GUI path was correct. The old/reference SSM route was then rendered from the same real bank and
firmware-driven note stream for96,000 final PCM frames: S3528 bypass RMS
0.08808125V, firmware-code RMS0.08847685V (+0.449%), forced-code35
RMS0.08594860V. Both enabled cases changed95,172 words; firmware-case
difference RMS/reference RMS=0.5543 (includes phase/delay). These are no-gain-
compensation comparisons, not a subjective loudness or spectral-error metric.
UART12/12,overrun0. This proves S3528 reaches this reference renderer with
real firmware controls; it does **not** prove the user's Windows checkbox in
an older build works. Reproduce with `tools/reconstruction_bank_probe.cpp`
and caller-supplied ROM, bank and profile. No claim of Windows runtime
validation is made.

## Controls and persistence

Filter service has two new groups. Native calibration requires nonlinear96/192k;
the S3528 lab controls now apply to Legacy and nonlinear modes.

* **Native DAC / deglitch**: post-chain level trim, DAC reference R/time,
  both I/V resistors, C187, sample/feedback resistors, typical switch Ron,
  C74 and C83. Values are converted to analytical RC coefficients on calibration
  changes; no new high-frequency digital scheduler loop is introduced.
* **S3528 test (lab)**: enable a renderer-only code override and select integer
  code0..63. Code35 gives3205.128Hz. The ordinary S3528 checkbox still controls
  bypass. Override OFF restores the firmware's code. Firmware registers and
  emulated clocks are never changed by this lab control.

Each new group has **Reset defaults** for that group only. Native reset keeps
existing SSM/VCF/VCA tuning and the S3528 lab setting. S3528 reset disables its
lab override. **Reset all** remains the original full calibration reset.
Defaults are schematic nominal/typical model values, **not hardware-measured
1:1 calibration**. The native monitor trim defaults to0dB and is applied after
VCF, S3528, VCA, mixing and host AC coupling, before the host limiter. It does
not change VCF/VCA drive; it also cannot correct a frequency-response error.

All67 parameters autosave, export/import and copy as schema2. Complete old
54-parameter schema1 profiles remain accepted; importing them retains the new
controls. Preferences write both schema1 (for0.70 compatibility) and schema2;
loading merges old chip values while keeping the extended native/lab controls.

Manual state capture now reports each hardware S3528 code, known mask and
nominal cutoff. Service monitor reports active native and S3528 route flags.

## Validation

* Independent1ns RK4 vs analytic state/area at nominal values, modified RC,
  unequal C74/C83 and coincident I/V/sample poles: PASS.
* Native final PCM at96/192k: repeat and0.5/1/2ms chunk invariance, gap/invalid
  recovery, no transport loss: PASS. +20dB post-chain trim preserves the exact
  solver samples/iterations/fallback/recovery/held trajectory: PASS.
* Schema1 migration, schema2 roundtrip, strict rejection, independent resets:
  PASS. S3528 test override equals actual code35 bit-for-bit on both native and
  reference voice routes at96/192k: PASS.
* Frozen0.68 nonlinear and frozen0.70 Legacy voice bypass unchanged: PASS.
* S3528 final PCM: Legacy6000/6000 words changed; nonlinear5999/6000.
  Session command/apply and model-switch continuity: covered by panel tests.
* ASan/UBSan analytic RC tests: PASS (leak detection unsupported/off).
* Win64 O3/LTO/NDEBUG, diagnostics0 cross-build, warnings treated as errors:
  PASS. This is compilation, not a Windows listening/performance test.

Remaining: verify the user's GUI path on Windows, compare actual differential
sample reconstruction against independent conversion/real hardware evidence,
and establish native absolute level/bandwidth before enabling it by default.
