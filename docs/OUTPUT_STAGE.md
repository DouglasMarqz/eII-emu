# EII output stage, common level model, individual outputs

Implemented in `src/level_model.h` and applied to every VCF/VCA model:
Historical Legacy, Legacy + Q, SSM2045 96k/192k, EIII CEM and SSM2045 model 2.
Regressions: `tests/model_level_tests.cpp` (engine level, absolute level from
the schematic) and the real-plug-in census `vst3_host_test ... levels`. Data:
`validation/output-stage-2026-10-29/`.

All schematic and datasheet facts below were read from rendered pages. OCR
was used only to locate pages. None of the datasheets are committed.

## 1. Board facts (rendered pages)

| Stage | Part / values | Gain, swing | Page |
|---|---|---|---|
| DAC I/V | IC52 LF356, R17 2.49k ∥ C187 220 pF | about ±1.3 V full scale | 7-45 (PDF107) |
| Deglitch | IC74 13202, C74 470 pF, R26 200 R, R34 10k, **IC73 TL084** with C83 470 pF | leaky integrator `y[n] = 50/51 y[n-1] - 50/51 iv[n]` reconstructs the DPCM data; ±13.5 V | 7-45; TI SLOS081O table 5.8 (PDF14): VOM ±13.5 V typ at 10k, ±15 V |
| VCF | IC94 SSM2045, R138 68k into SIG IN | pin 8 = -8.1k/68k = -0.1191 per Y volt | 7-45; SSM2045 PDF1 Fig. 1 |
| S3528 input stage | IC84 S3528 SIG/SIGFB amplifier, R66 4.7k, R58 10k, C172 100 pF | -2.128; chip on +5VA/-5V | 7-45; S3528B PDF2 (p.45): max input / output 2.1 Vrms typ (2.97 V peak) at 10 V supply |
| VCA | SSM2045 VCA2, R74/R82 10k/470 R, control via -BIAS | 8.17 S/A x 500 µA x 470/10470 | 7-45, 7-52 |
| Channel output | S3528 BUFOUT amplifier, R154 4.7k ∥ C161 1 nF, drives **+CnOUT.V** | -4.7k x I; ±2.97 V (S3528) | 7-45 |
| Individual outputs | R156..R163 1k from C7OUT..C0OUT to **J1..J8** (J1 = channel 7 ... J8 = channel 0); sleeves on CN6-2 ground | unity; no switching jacks | 7-52 (PDF114) |
| Mix amplifier | R164..R171 10k into **IC96 MC34004** (quad JFET, ±15 V), C148 470 pF from sum node to output | -Σ CnOUT x RT5/10k; pole 33.9 kHz at full volume | 7-52; Motorola DL128 1983 p.3-198 (PDF275): VO ±14 V typ RL≥10k, ±13 V typ RL≥2k |
| MIX OUT VOLUME | **RT5 10k log**: CW end tied to the wiper, so it works as a rheostat in the IC96 feedback (CN6-1 sum node, CN6-3 output). Parts list: "10 kohm Pot RT5". Manual: "This rotary pot is in the feedback loop of the Mix Out amplifier... It doesn't affect the volume of individual channel outputs" | 0 (CCW) ... -1 (full CW) | left panel 7-5 (PDF68); PDF7; PDF146 |
| Main out | R115 1k from IC96 to **J9** (mono MIX OUT). There is no further line driver. | unity | 7-52 |

**Where the code stopped.** It modelled the chain up to `+CnOUT.V`
(`Channel1Feedback`, R154/C161) and summed the eight channels directly into
the host, at 1 V = 0 dBFS. It had no S3528 or op-amp rails, no mix amplifier,
no individual outputs, and a ±1.0 host clamp. All the old models also carried
the VCF output in the vendor y-scale: SIG IN modelled as a 900 R/68.9k
divider, so pin 8 was 9.12x (19.2 dB) too low. The S3528-ON route had an
extra x0.47 match, and model 2 an extra x0.10966 match. Together that is
-25.76 dB against the board. **That is why the main output was quiet.**

## 2. Model now (`level_model.h`, all models)

* VCF output at real pin-8 volts: x `vendorToPin8` = -9.1191 for the
  vendor/Legacy/EIII/Historical filters. Model 2 is already at pin level.
* IC73 TL084 ±13.5 V soft limit: on the integrator state (hardware DAC
  route) and on Y (no-DAC route).
* S3528 input stage -2.128 with the S3528 ON **and** OFF, then the S3528
  2.97 V-peak swing. OFF removes only the filter. One common level; the 0.47
  match is removed.
* Channel output CnOUT.V = BUFOUT, limited to ±2.97 V.
* 7-52 mix amplifier at the voice-bank rate (96/192k, 48k for Historical):
  -Σ CnOUT x 10k/10k, C148 pole 33.86 kHz, MC34004 ±13.5 V soft limit.
  RT5 is at full CW (unity); the plug-in Volume stands in for it after the
  stage.
* Soft knee for every rail: `y/(1+(y/V)^16)^(1/16)`, within 1% of linear up
  to 0.88 V. The datasheets give only the swing.
* Hardware DAC + deglitch OFF = no DAC: raw digital codes. This route is
  unchanged except that its level is matched to the hardware route at 1 kHz
  (-21.17 dB = the integrator's 1 kHz gain, measured in
  native-response-071). It keeps the codes inside the rails.
* **Host calibration (decision):** 1 V peak at MIX OUT (J9), full volume =
  0 dBFS. This is the same reference the sampling input already uses (+INPUT.V
  1 V peak = host 0 dBFS), so a unity path through the box reads unity in the
  host. The individual outputs use the same scale: 1 V at Jn = 0 dBFS. The
  ±1.0 clamp is removed. The MC34004 rails bound the signal, and the host
  receives float headroom: up to +22.6 dBFS, the 13.5 V rail.
* Final host output gain: 0..+24 dB (was 0..+12). The state stores dB, so old
  projects are unchanged. Host automation in normalized units now spans 24 dB.

Engine-level absolute check (`model_level_tests`): a DPCM 1 kHz tone of 2 V
peak at Y gives -10.21 dBFS rms on paper (2 x 0.1191 x 2.128 x 0.8616 =
0.437 V peak). Measured: SSM -10.57, Legacy+Q -10.54, EIII -10.59, model 2
-10.98 dBFS, all within 1 dB. The models agree within 0.28 dB on the hardware
route. S3528 ON vs OFF differs by 0.01 dB.

## 3. Individual outputs (VST3)

The board has eight per-channel jacks, J1..J8, plus the mono MIX OUT (J9).
The plug-in exposes the main stereo bus (MIX OUT, mono on both sides) and
eight mono aux buses, "J1 Out (ch 7)" .. "J8 Out (ch 0)". The aux buses are
**inactive by default**. While no aux bus is active, the DSP does no
per-channel work. When one is active, each channel's CnOUT.V is decimated
like the mix, DC-blocked like the mix, and rendered sample-aligned with the
main bus, including sample-rate conversion. They are not inverted, unlike the
mix. Volume and final output gain apply to all buses.

## 4. Measured levels, before -> after

**Sampling loop** (`tools/sample_wav_probe`, real OS3.1). A 0 dBFS 440 Hz
sine goes to +INPUT.V at 1 V peak, then FORCE sampling, then the channel-0
monitor and the playback of note 48. Default calibration. Logs:
`wav_before.log`, `wav_after.log`.

| Engine | Monitor before | Monitor after | Playback before | Playback after |
|---|---|---|---|---|
| Historical Legacy (no DAC) | -14.7 dBFS | -10.1 | -15.0 | -10.4 |
| SSM2045 96k, hardware DAC | -29.2 dBFS | **-3.5** | -29.8 | **-4.1** |

Schematic prediction for the hardware route: 1 V x 3.2 (IINT, DPCM loop) x
0.1191 x 2.128 x 0.8616 = 0.70 V, which is -3.1 dBFS. The measured -3.5 dBFS
agrees. So 0 dB into the sampler now comes back at about -3.5 dB, as on the
board at full volume. Before this change it came back at -29 dB, and the
owner compensated with up to +17 dB of trims plus the +12 dB final gain.

**Real plug-in, fresh instance with the owner's defaults.** These include the
registry and the global Filter Service trims of the old models: +16 to +18 dB,
migrated as described in section 5. Hardware DAC is ON (the default). MIDI
48/60/67/72. Values are rms / peak dBFS. Logs: `levels_before.log`,
`levels_after.log`.

| Model | Piano before | Piano after | Brass before | Brass after |
|---|---|---|---|---|
| SSM2045 96k (owner trims) | -30.67 / -13.82 | -30.88 / -15.08 | -23.29 / -9.93 | -23.97 / -13.71 |
| Legacy + Q (owner trims) | -31.41 / -14.54 | -31.41 / -14.55 | -24.02 / -10.63 | -24.02 / -10.65 |
| EIII (owner trims) | -30.73 / -13.79 | -30.75 / -13.90 | -23.25 / -9.86 | -23.31 / -10.14 |
| SSM2045 model 2 (no trims) | -48.38 / -31.73 | **-22.62 / -5.98** | -41.06 / -28.12 | **-15.31 / -2.36** |
| Historical (no trims, no DAC) | -26.00 / -4.97 | -21.41 / -0.37 | -12.25 / 0.00 (clamped) | -7.39 / +9.42 |

Untrimmed models now play at the board level. The owner's deliberately trimmed
old models keep their previous loudness: their NativeOutDb is lowered by
25.76 dB. Resetting their native trim, InputGain and MixGain in Filter
Service brings them to the board level, like model 2. The no-DAC route has a +6 dB/oct tilt on
the DPCM data, so its peaks can exceed 0 dBFS. The host receives floats.

## 5. Stored trims (migration)

The level change is +25.76 dB for the old models (x9.12 / 0.47) and for model
2 (/0.10966 / 0.47). A native trim the user set deliberately (non-zero
NativeOutDb) in a project state, the global Filter Service profile, the
standalone registry or an exported profile is lowered once by 25.76 dB
(clamped at -24 dB). So that loudness stays as it was. Untouched (0 dB) trims
follow the new hardware level. Markers: `levelmodel=2`,
`EII_FILTER_SERVICE_GLOBAL=4`, `EII_FILTER_CALIBRATION=5`. InputGain and
MixGain are not migrated.

## 6. Settings coherence (VST3)

* **Precedence.** For a new instance with no project state, global defaults
  apply: registry preferences plus the global Filter Service profile, now
  including model 2. A project state replaces every setting.
* **The menu shows the applied state.** `configure()` applies model, DAC
  route, S3528 and calibration to the running engine at once, and the
  commands are idempotent. "Requested, not active" marks any difference. In
  Historical Legacy the hardware DAC item is greyed, because that model has no
  native route. The status text reports `appliedmodel`, `appliednative` and
  `applieds3528`.
* **Persistence and autoboot,** checked by `vst3_host_test levels`
  (SETTINGS lines): model, route, S3528 and autoboot persist. autoboot=0 does
  not boot. autoboot=1 boots and applies the project's settings. A later
  project state is applied to the running engine.
* **Reboot-needed items are marked:** ROM folder, HDD, direct writes and fast
  panel are "(next boot)". The margin is "(next activation)".

## 7. S3528

OFF keeps the 7-45 input stage and removes only the filter, with one level.
ON vs OFF: 0.00 dB at 300 Hz and 0.01 dB at 1 kHz. The audible difference is
the filter alone: it cuts highs above the cutoff the firmware sets from the
sample rate (ANALOG_AUDIT_S3528_SSM2045.md A4/A5).
