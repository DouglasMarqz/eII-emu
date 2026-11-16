# Analog audit: S3528 reconstruction stage and SSM2045 (2026-10-29)

Branch `custom/eiii-cem3387`, base 050e13c. Fix commits: 649c042 (S3528
input stage), a39d081 (VCA -BIAS threshold, Q 1.8k), ce2e306 (S3528 host level
match). All schematic and
datasheet statements below come from rendered pages viewed as images. OCR was
used only to find pages.

## Sources

| Source | Identity | Pages used |
| --- | --- | --- |
| EII service manual `eii_service.pdf` | SHA256 f1071d34…0700031 | PDF74 (IC86 74LS138), PDF103 (IC6 74HCT138), PDF107 = sheet 7-45 (output channel 1), PDF108 = 7-46 (channel 2 cross-check), PDF114 = 7-52 (mixer, -BIAS) |
| AMI S3528B, Feb 1993 (`63928.pdf`) | SHA256 6f6deb60…e47c9e | PDF1/p44 (block diagram, pinout), PDF2/p45 (analog parameters), PDF4/p47 (pin functions), PDF5/p48 (Table 1.0, fc law) |
| SSM2045, SSMT 1983 (ka-electronics) | SHA256 60a0340c…e22442d | PDF1 (Fig. 1 block diagram), PDF2 (specifications), PDF3 (Fig. 2, text), PDF4 (Fig. 4), PDF5 (Fig. 6a/b), PDF6 (response equations) |
| Vishay 1N914 doc. 85622 rev 2.2 | SHA256 d6ba700d…16940f | PDF2 Fig. 1 (VF vs IF) |

Copies live in `F:\Claude\_assets\datasheets\` (S3528B_AMI1993_63928.pdf,
SSM2045_SSM1983.pdf, 1N914_Vishay_85622.pdf). They are not committed.

## A. S3528

### A1. Board wiring (7-45, IC84; 7-46 has the same values)

| Item | Rendered schematic / datasheet | Code | Verdict |
| --- | --- | --- | --- |
| Part | IC84 S3528, one per channel | `S3528Control` ×8, machine.h:92 | match |
| Data | +FD0..FD5 → LSB(3), D1(2), D2(1), D3(18), D4(17), MSB(16) | `completedWrite(v)`, `v&63`, machine.cpp:209-210 | match |
| Strobe | OUT 40h-47h: IC86 Q0 = -PCSWRF (PDF74) → IC6 74HCT138, PA2..0 → -CSWF0..7 (PDF103) → CE pin 4 | `(p&0xf8)==0x40`, channel `p&7` | match |
| CE semantics | -CSWF low while selected, high after the write; AMI: "when CE is strobed the data … will be latched" (1993 PDF5); three-level CE per the 1984 catalogue (SWITCHED_ANALOG.md) | `drive(LoadGround)` then `HoldVdd`, s3528.h:26-28 | match |
| Clock | +C5MHZ.C → OSCI pin 13; OSCO pin 14 not connected | 5 MHz constant, s3528_runtime.h:44 | match (nominal 5 MHz) |
| Divider ROM | Table 1.0, all 64 codes (PDF5) | `divisors[64]`, s3528.h:11-15 | all 64 match |
| Cutoff law | fc = fCLK/(40·N); internal sampling fCLK/N (PDF5) | `5e6/(40*N)`, bilinear prewarp, s3528_runtime.h:42-47 | match |
| Supplies | +5VA pin 6, -5V pin 5, AGND 11, DGND 15 | not modelled (no rail clipping) | limitation |
| **Input stage** | SSM2045 4P (pin 8) → **R66 4.7k** → SIG IN pin 12 (inverting input of on-chip amp, + at AGND, PDF1/PDF4); **R58 10k** FB(10)→SIG IN; C172 100pF FB→ground | **missing (unity gain)** | **fixed in 649c042**: gain -R58/R66 = -2.128 (+6.56 dB, inverted), s3528_runtime.h:11, nonlinear_voice.h:84, runtime_audio.h:112-113 |
| Output | FLT OUT pin 9 → C93 1µF → R74 10k / R82 470 → SSM2045 MIX2 +IN pin 12 | nonlinear_voice.h:90-91, runtime_audio.h:117-118 | match |
| Output buffer | SSM2045 MIX OUT pin 13 → BUFF IN pin 8; R154 4.7k ‖ C161 1nF to BUFF OUT pin 7 = +C1OUT.V | `Channel1Feedback`, ssm2045.h | match |

C172 sits from the amplifier output to ground. With an ideal amplifier it adds
no audio-band pole, so it is not modelled. If it were across R58 instead, the
pole would be at 159 kHz, which would also be inaudible.

### A2. Signal chain

DAC IC60 → LF356 I/V → 13202/TL084 deglitch → R138 68k → SSM2045 VCF → **S3528
(R66/R58 amplifier, cosine prefilter, 7th-order elliptic)** → C93/R74/R82 →
SSM2045 VCA2 → S3528 BUFF amplifier as I/V (R154/C161) → +C1OUT.V → mixer
(7-52). The code keeps this order in every route: the S3528 comes after the
VCF tap and before the coupling and VCA.

### A3. Does the toggle reach every route?

Yes. It reached every route before the fix as well. Evidence (C++ tests, Windows
LLVM-MinGW 20261006):

* `tests/analog_audit_tests.cpp`: firmware code 00 (61 Hz) against code 63
  (31.25 kHz) attenuates a 1 kHz tone by more than 30 dB in every route:
  NonlinearPlaybackVoice (SSM 96k/192k, Legacy+Q, EIII; reference and native
  paths), PlaybackVoiceBank (reference `reconstructionCode` and native
  `NativePins::sw`), and RuntimeAudioEngine for all five modes (BasicVoice,
  SSM96, SSM192, LegacyQ, EiiiCustom). The ON/OFF small-signal ratio is
  -2.12 ±0.01 in each route.
* VST3: `PluginEngine::installAudio` re-applies `settings_.reconstruction`
  after every model switch (plugin_engine.h:272), and the state key is `s3528=`
  (plugin_state.h:51/75).

### A4. Codes that the firmware actually programs

Measured with `tools/analog_audit_probe.cpp` (real ROMs, production controller,
MIDI notes 36…108, 0.7 s each, velocity 110):

| Bank | Note → S3528 code (fc) | FCV (VCF f1 / f2, EII caps) |
| --- | --- | --- |
| 18 Grand Piano #2 | idle 59 (12.5 kHz); 36 → 47 (7.81 kHz); 48 → 61 (20.8 kHz); 60/72/84 → 59 (12.5 kHz); 96 → 61 | 0.74-0.90 V (f1 6.8-8.6 kHz, f2 10-12.7 kHz); note 36: 1.25 V (f1 4.0 kHz) |
| 47 Big Brass & Cymbal | 36 → 57 (5.43 kHz); 48/60/72 → 59; 84 → 62 (25 kHz); 96/108 → 61 | 0 V (VCF wide open, f1 26.6 kHz) |

The firmware writes the code once per voice assignment, so each channel shows
`writes=2` (one at init, one at the note). The cutoff follows the sample's
recorded rate, not the played pitch.

### A5. Measured difference (filter only, unity-gain build 050e13c)

The values are band energy change ON/OFF in dB per 0.65 s note window
(Welch, 4096). The level shown is that band's share of the note energy with
S3528 OFF. SSM96 reference route; the native route behaves the same.

| Note | ≤5 kHz | 5-8k | 8-12k | 12-16k | 16-20k |
| --- | --- | --- | --- | --- | --- |
| Piano 36 (7.8k) | +0.2 | -40 dB / +0.1 | -51 dB / **-7.2** | -65 dB / **-55** | -74 dB / **-51** |
| Piano 60 (12.5k) | ≤+0.1 | -21 dB / +0.2 | -33 dB / +0.1 | -44 dB / **-4.2** | -48 dB / **-53** |
| Brass 36 (5.4k) | +0.2 | -28 dB / **-5.0** | -28 dB / **-52** | -23 dB / **-53** | -32 dB / **-59** |
| Brass 60 (12.5k) | +0.1 | -13 dB / +0.2 | -19 dB / +0.1 | -23 dB / **-4.1** | -26 dB / **-41** |
| Brass 84 (25k) | 0.0 | +0.1 | +0.1 | +0.2 | +0.1 |

Whole run (360000 frames): below 8 kHz the change is within 0.25 dB in every
route. In 8-12.5 kHz it is +0.2 dB or less, except the Legacy 48k route
(-0.6 dB in 8-10 kHz and -1.1 dB in 10-12.5 kHz on the piano). Full tables:
`validation/analog-audit-2026-10-29/spectra.txt`. The passband group delay is about 1-2 samples at
48 kHz. Delay-aligned
residuals:

| Route | Piano | Brass |
| --- | --- | --- |
| Reference | -14.8 dB | -7.6 dB |
| Native | -22.4 dB | -13.1 dB |

### A6. Why the toggle is hard to hear

1. With the codes above, the S3528 cuts at or above the content that is left.
   Most piano and brass notes use 12.5-25 kHz. The FIR at 20 kHz and
   the hearing limit already cover 16-20 kHz.
2. On the piano the SSM2045 VCF is the dominant lowpass. At FCV 0.82 V the EII
   4-pole is -9.6 dB at 12.5 kHz and -25 dB at 20 kHz. Note 36 (S3528 at
   7.8 kHz) has its VCF at 4 kHz: -32 dB at 12 kHz. On the notes where the
   S3528 actually cuts (36 and 60-84), the energy it removes is already -39 to
   -74 dB (reference route) and -64 to -88 dB (native route) below the note.
3. An audible effect does appear for low-rate samples played with the VCF
   open. Brass note 36 (code 57, 5.4 kHz, FCV 0) loses 50+ dB of image and
   aliasing energy that sits at -23 to -28 dB in the 8-16 kHz bands. That is
   the stage's real purpose.
4. Before 649c042 the ON route also lacked the +6.56 dB inverting input stage.
   Without it, ON and OFF had the same level.

### A7. Fixes 649c042 and ce2e306

**649c042** puts the board's -R58/R66 = -2.128 in front of the S3528 kernel
(s3528_runtime.h:11, nonlinear_voice.h:84, runtime_audio.h:112-113). The ON
output became 2.13× and inverted:

* Grand Piano RMS OFF/ON: legacy48 0.0555/0.1179, SSM96 0.0335/0.0713.
* With default trims, loud banks clipped at the host clamp. Big Brass
  clipped frames (of 360000):

  | Route | 649c042 | a39d081 |
  | --- | --- | --- |
  | SSM96 | 1256 | 3057 |
  | Legacy+Q | 11944 | 14282 |
  | EIII | 8239 | 10302 |
  | legacy48 | 23128 | 23128 |

**ce2e306** therefore scales only the S3528 route's channel output by
R66/R58 = 0.47 after the buffer I/V (`s3528HostLevelMatch`). The absolute
host level is an arbitrary 1 V reference, not hardware, so this is labelled
as a host-reference choice, not a circuit element. With S3528 ON:

* Loudness matches the bypass.
* The SSM2045 VCA receives the board's 2.13× drive into its 0.211 V soft
  limit.
* Polarity is inverted (physical).

Bypass is bit-identical at every step (fingerprints below). Real-bank RMS
OFF/ON at ce2e306:

| Route | Piano | Brass | Brass clipped (ON) |
| --- | --- | --- | --- |
| legacy48 | 0.05546/0.05543 | 0.2354/0.2312 | 2525 (OFF already clips) |
| SSM96 | 0.03648/0.03651 | 0.1572/0.1560 | 0 |
| SSM192 | — | 0.1568/0.1557 | 0 |
| native SSM96 | 0.004353/0.004356 | — | 0 |

### A8. Remaining S3528 limitations (unchanged)

These are not modelled:

* SC clock images and feedthrough; output offset (±80 mV typ, removed by C93
  anyway); rail clipping (VFS 2.1 Vrms).
* Divider phase.
* The pair-average and hold of the laboratory model.
* Source-clock deglitch dynamics at runtime.

The 5 MHz master is nominal. The response is the AMI 1993 typical fit scaled
from 3.2 kHz (S3528_CURVE_FIT.md).

## B. SSM2045

### B1. Topology against the datasheet (PDF1 Fig. 1, PDF6)

The datasheet gives each 2-pole section as
H = -(gm²/(C1·10C2)) / (s² + s·gm/C1 + gm²/(C1·10C2)). The section gain of 10
behind C2 comes from the 8.1k:900 divider. The 4P output is non-inverted and
the 2P output is inverted.

* Vendor model (`vendor/ssm2045-chip`, chip.cpp:207-231). G1 integrates
  (u + y1 - x1 - ga), G2 is inverting, y = x/10, and section 2 is fed from y1.
  Deriving the small-signal transfer reproduces the datasheet H exactly. Pole
  count: 4. Discretisation: trapezoidal, implicit Newton (zero-delay).
* `LegacyQFilter` (legacy_q_filter.h). Same linear equations, solved
  analytically, with only the Ga feedback nonlinear.
* `Ssm2045OpenLoop` (ssm2045.h; Legacy 48k). Two TPT SVF sections with
  g = gm/(2fs·√(C1·10C2)) and damping √(10C2/C1). This is the same H, without
  frequency prewarping: about -8 % at 8 kHz.

### B2. Board components (7-45, PDF107)

| Item | Schematic | Code | Verdict |
| --- | --- | --- | --- |
| C1/C2/C3/C4 (pins 3/4/6/7) | C153 0.02µ, C144 3.9n, C128 0.02µ, C120 1800p | nonlinear_voice.h:30, ssm2045.h Caps | match |
| Signal input | deglitch Y → R138 68k → SIG pin 2 (900 Ω internal) | `900/68900`, nonlinear_voice.h:79 | match |
| FC pin 5 | R122 22.6k from FCV, R130 124k to -15 V, R112 1k to ground, C136 3.3µF to ground | nonlinear_voice.h:51-53, 95-96; FcRin/FcRbias/FcRshunt/FcCap defaults | match |
| Q pin 17 | R146 22k from QCV | Qres 22 | match; see B4 |
| VCA1 | pin 11 (MIX1 -IN) and pin 16 (LIN MIX1) grounded | `process(0, …)`, I16 = 0 | match |
| VCA2 control | ACV → R91 4.7k → C112 1µF to ground → R100 4.7k → pin 15 | vca_board_control.h | match; see B6 |
| VCA2 input | pin 12 ← R74 10k / R82 470 from C93 | nonlinear_voice.h:91 | match |
| EXPO pin 14 | -BIAS.V (7-52) | was not modelled | **fixed a39d081** (B6) |

The resulting section parameters are:

| | Q1 | Q2 | f2/f1 |
| --- | --- | --- | --- |
| EII | 0.716 | 1.054 | 1.472 |
| Datasheet Fig. 2 | 0.550 | 1.213 | 2.203 |

### B3. Cutoff law, temperature, range

* Pin 5 node: V5 = (FCV/22.6k - 15/124k)·951 Ω = 0.0421·FCV - 115 mV. FCV
  0…5 V maps to -115…+95 mV.
* gm = 72.188 µS·exp(-(V5 - 0.98 mV)/27.844 mV). This is -19.3 mV/octave
  (datasheet typical, PDF2). The gm0 anchor reproduces the datasheet's
  "about 20 Hz-20 kHz for ±90 mV" with the Fig. 2 capacitors (geometric centre
  632 Hz).
* With EII capacitors, f1 runs from 26.6 kHz (FCV 0) to 14 Hz (FCV 5).
* Temperature is fixed at 25 °C. The datasheet gives only the 25 °C typical.
* Bias: 1 µA × 951 Ω = 0.95 mV against the 0.982 mV used. That is about 0.1 %
  in frequency, so it was not changed.
* The Legacy 48k route uses its own provisional anchor: 678.8 Hz at FCV 2.5 V,
  against 607 Hz from the vendor law. This is +0.16 octave (runtime_audio.h:83),
  a documented provisional value. Not changed.

### B4. Q input — fixed a39d081

PDF1 Fig. 1 shows pin 17 → 1.8k → emitter of a grounded-base PNP. The PDF3
text says the source must exceed +600 mV and calls the 1.8k "short circuit
protection".

* Before: I = (QCV - 0.6)/22k. Now: (QCV - 0.6)/(Qres + 1.8k),
  nonlinear_voice.h:57-58 and :71.
* The current is 7.6 % lower: at QCV 5 V, 184.9 µA instead of 200 µA.
* Filter Service "Q input R" 20.2 kΩ restores the old current.

Using the vendor's linear Ga loop (W1 2.10216, W8 3.10216), the oscillation
onset is:

| Capacitors | IQ at onset | In-band gain at onset |
| --- | --- | --- |
| Datasheet Fig. 2 | 150 µA | -6.0 dB (matches the datasheet's "6 dB") |
| EII | 73 µA | -3.5 dB |

The EII onset corresponds to QCV 2.35 V. W1/W8 are calibrated, not published,
so the EII onset is a model prediction.

### B5. Levels and saturation

* Datasheet (PDF2): input 150 mVpp typ / 300 mVpp max at pin 2; output
  1 Vpp typ / 3 Vpp max.
* In the model, the board deglitch DC gain (-R34/R26 = -50) times 900/68900
  can drive pin 2 to about ±3 V at full-scale DAC and ACV 5 V. That is far
  above the datasheet input.
* The OTA tanh limits (G1 0.145 V, G2 0.26 V) and the output limit (±0.7 V at
  the taps) bound it. Rail clipping of the TL084 deglitch (±13 V) is not
  modelled. This is pre-existing and documented in PLAYBACK_ANALOG.md, and is
  not a regression.

### B6. VCA control — fixed a39d081

7-52 (PDF114) wiring:

* +15 V → R114 22k → D5 1N914 → ground.
* R105 10k → IC96 MC34004 inverter, with R104 4.7k feedback. The output is
  -BIAS.V, which goes to pin 14 of all eight SSM2045s.

Value: VF ≈ 0.572 V at 0.66 mA (Vishay Fig. 1, 25 °C typical), so
-BIAS = -0.269 V.

In the datasheet Fig. 4 connection, pin 14 is the base of the VCA2 control
transistor. The "off within about 500 mV of ground" therefore moves to
0.5 - 0.269 = 0.231 V (vca_board_control.h:39-40).

Effect on branch current:

| ACV | Before | After | Change |
| --- | --- | --- | --- |
| 5 V | 456.9 µA | 484.1 µA | +0.50 dB |
| 1 V | 50.8 µA | 78.1 µA | +3.7 dB |
| 0.231-0.5 V | VCA closed | VCA open | — |

Release tails therefore continue below the old cutoff.

Real-bank OFF RMS, a39d081 against 649c042:

| Route | Piano | Brass |
| --- | --- | --- |
| SSM96 | 0.03347 → 0.03648 (+0.75 dB) | 0.1462 → 0.1572 (+0.63 dB) |
| native SSM96 | 0.003988 → 0.004353 (+0.76 dB) | — |
| legacy48 | unchanged | unchanged |

The vendor's EXPO crossfade (w1/w2) describes the Fig. 6a common-current
connection. On the EII, pin 16 is grounded, so that crossfade must stay at
V14 = 0. The board bias is represented as the threshold shift instead. Do not
set the Filter Service "VCA EXPO voltage" to -0.269 V: that would starve VCA2.

### B7. Deviation list

| # | Sheet/page | Code | Status |
| --- | --- | --- | --- |
| 1 | 7-45 R66/R58, S3528 PDF1/4 | s3528_runtime.h:11; nonlinear_voice.h:84; runtime_audio.h:112 | fixed 649c042 (S3528 route only) |
| 2 | 7-52 R114/D5/R105/R104 → pin 14 | vca_board_control.h:39-40 | fixed a39d081 |
| 3 | SSM2045 PDF1 1.8k, PDF3 | nonlinear_voice.h:57-58, :71 | fixed a39d081 |
| 4 | SSM2045 PDF2 300 mVpp max vs board drive | nonlinear_voice.h:75-79 | open, pre-existing (deglitch rails) |
| 5 | Legacy 48k FC anchor 678.8 Hz vs vendor law 607 Hz | runtime_audio.h:83 | open, documented provisional |
| 6 | FC bias 0.982 mV vs 0.951 mV | FcBias default | negligible, unchanged |
| 7 | 7-45 C172 to ground, S3528 rails/offset | — | not modelled, inaudible |

## Gates (Windows, LLVM-MinGW 20261006)

Logs are in `validation/analog-audit-2026-10-29/`. The pass and fail lists are
the same at 050e13c, 649c042, a39d081 and ce2e306, apart from the new
`analog_audit_tests` (PASS from 649c042).

**`tools/build-win64-vst3.ps1 -SkipValidator`**

* The DLL builds. SHA256: a39d081 2679025B…44E540, ce2e306 5C69321F…D54405.
* The script then stops at `vst3_engine_tests`: exit 0xC00000FD, a stack
  overflow with the default 1 MB main stack. This was reproduced at 050e13c
  (`engine_base.log`).
* The same test sources linked with `-Wl,--stack,268435456` PASS:
  `vst3_engine_tests`, `filter_service_lifecycle_tests`, `vst3_input_tests`,
  `filter_service_tests`. This covers 649c042, a39d081 and ce2e306. The
  script itself is unchanged, per the build-recipe policy.

**Unit tests at -O2 (`gate_*.log`)**

* PASS: `analog_audit`, `s3528_runtime`, `s3528_pcm`, `s3528_curve`,
  `chip_audio`, `cem_service`, `vca_board_control`, `eiii_custom`,
  `native_audio`, `native_pcm`, `runtime_audio`, `playback_analog`,
  `filter_recovery`, `filter_stiff`, `sampling_monitor`.
* Already failing at 050e13c:
  * `s3528_voice_tests` ("bypass differs from frozen0.68") and
    `legacy_q_tests` ("accepted nonlinear SSM voice remains bit-identical"):
    their frozen 0.68 references predate the VCA020 board control.
  * `filter_service_tests`: 1 MB stack overflow (PASS with the big stack).
  * `switched_analog_tests`: compile error.
  * `filter_solver_tests`: missing reference symbols.

### PCM fingerprints

From `tools/model_fingerprint_probe.cpp`: FNV-1a over 12000 frames, eight
voices, changing DAC/ACV/FCV/QCV, Windows O3/LTO. A dash means the value is
the same as the previous column.

| Route | 050e13c | 649c042 | a39d081 | ce2e306 |
| --- | --- | --- | --- | --- |
| legacy48 off | 8f54fb4bcd693ee6 | — | — | — |
| legacy48 on | 4816107ac2f0c07b | a29baf9b300a947a | — | e9167e98f18859fb |
| ssm96 ref off | b09144fb2a504811 | — | 238e05ba08dbf298 | — |
| ssm96 ref on | 802d48316d8d1eb9 | 5a3aac9705aa2070 | bc778df229b38440 | dd84d67d223e064 |
| ssm96 native off | 4723dffd0f393013 | — | bf0386c0e3ab0e1 | — |
| ssm192 ref off | 8ef460ca30db9bf5 | — | 485d58e32530a888 | — |
| ssm192 native off | 2cd11cae63ecf0a4 | — | 9473e5f5928f2560 | — |
| legacyq96 ref off | 69dff4066b3d12a8 | — | d24c1d9040c6a608 | — |
| legacyq96 native off | c5297a01e2c14846 | — | 5e69411c82f70ba5 | — |
| eiii96 ref off | b2845ba5b307acf8 | — | 4ca3124ec59df87a | — |
| eiii96 native off | 731be7dd44fd41e2 | — | 619adbe4a6011d15 | — |

The ON fingerprints of the other routes change at each fix commit; they are
listed in the logs.

Real-bank OFF PCM (`analog_audit_probe`, Grand Piano #2 and Big Brass) is
identical between 050e13c and 649c042 for all nine routes, and between
a39d081 and ce2e306. a39d081 changes it in every route except legacy48 (B6).

## C. New independent SSM2045 model

Not started. A and B took the session, and the fixes above change the
reference that a new model would be validated against. A proposed scope for a
later session:

* An OTA cascade derived from Fig. 1, including the 8.1k:900 dividers and the
  Ga summing node.
* A zero-delay solve with 2× internal oversampling.
* A new `RuntimeAudioMode` appended after EiiiCustom, with its own calibration
  array (plugin_state model index stays backward compatible).
* Validation against datasheet PDF6 H(s), -19.3 mV/oct, the 150 µA onset with
  Fig. 2 capacitors, and the 6 dB in-band loss.
