# SSM2045 model 2 (independent block-diagram model)

Status: new, selectable, additive. Menu: "SSM2045 model 2 - block-diagram VCF / VCA
(96k/192k)". The existing models (Legacy 48k, Legacy+Q, SSM2045 96k/192k vendor
chip, EIII CEM3387) are bit-identical to ce2e306 (fingerprints in
`validation/ssm2045-model2-2026-10-29/gate.log`).

Sources. Every datasheet statement here was read from rendered pages (zoomed
images). OCR/text extraction was used only to find pages. SSMT 1983 SSM2045
datasheet (`SSM2045_SSM1983.pdf`, SHA256 60a0340c...e22442d, not committed):
PDF1 Fig. 1 (block diagram), PDF2 (specifications), PDF3 (Fig. 2 and text),
PDF4 (Fig. 4, VCA connection), PDF6 (response equations). EII service manual
sheet 7-45 (PDF107) for the board values, 7-52 (PDF114) for -BIAS.

The GLM SPICE reference (`glm_ssm2045_ref.zip`) was treated as a hypothesis. It
is not committed and nothing in model 2 is copied from it. Section 7 lists where
it disagrees with the datasheet and why model 2 differs.

## 1. Topology (PDF1 Fig. 1)

* SIG IN, pin 2, is a summing node, not a voltage input. It has 900 R to ground
  and 8.1k from the 2-pole buffer (pin 1). The external source resistor and the
  G_Q output also go into it. G_Q (+) is this node and G_Q (-) is pin 8.
* G1: (+) node, (-) C1 (pin 3), output into C1. This is a follower-integrator.
  G2: (-) C1, (+) ground, output into C2 (pin 4). Buffer B copies C2 to pin 1.
* Section 2 is the same: the node has 900 R to ground, 8.1k from pin 1 and
  8.1k from pin 8. G3/G4 with C3/C4, and B copies C4 to pin 8.
* One e^-x converter (pin 5) sets all four OTA tails.
* Q: pin 17 -> 1.8k -> grounded-base PNP. Its collector is the G_Q tail.
* VCA2 linear control: pin 15 -> 450 R -> PNP emitter. The base is pin 14
  (EII: -BIAS, -0.267 V from the 7-52 diode network).
* Pins 1/8 carry the real capacitor voltages. The "gain of 10" in PDF6 is the
  8.1k:900 return. The in-band gain is -8.1k/Rext. For EII R138 = 68k that is
  0.1191 (-18.48 dB) per deglitch volt.

Model 2 works at real pin levels. The vendor model and GLM run at about 1/10 of
them (their y = x/10).

## 2. Device physics and calibration

| Quantity | Model 2 | Anchor (rendered page) |
|---|---|---|
| OTA law | i = I tanh(a v / 2Vt), gm = a I / 2Vt, a = 0.433 for G1..G4 | one attenuation fits both PDF2 THD points (below) |
| Tail at V5 = 0 | 12.60362 uA | Fig. 2 (Rext 5k, 10n/3.3n/10n/680p): f3 = 632.5 Hz at V5 = 0 |
| Expo | -19.3 mV/oct at 25 C, PTAT scaling | PDF2 typ. |
| Pin-5 bias | 1 uA into the board Thevenin resistance | PDF2 typ. |
| G_Q | aQ = 0.10396, PNP law Veb(10 uA) = 0.6 V | Fig. 2 oscillation onset 150 uA; "source above +600 mV" (PDF3) |
| VCA cell | aVca = 0.41982 (8.17 S/A at 25 C) | PDF4 Fig. 4 gain; THD check below |
| Buffers | class A, 750 uA sink (soft knee) | PDF2 output sink current typ. |
| OTA compliance | +13.5 / -3.5 V (EII +15/-5, 1.5 V headroom) | 7-45 supplies |
| Noise | 23.7 nV/rtHz at G1 input | PDF2 S/N 84 dB re 150 mVpp, 20 kHz BW |
| Offsets (option, off) | Vos 1.5-2 mV, Ios 6-8 nA, VCA feedthrough 0.00275 x tail | limit-scaled typ., 41.5 dB control rejection |
| Deglitch TL084 | common ±13.5 V soft swing for every model (docs/OUTPUT_STAGE.md); Ssm2Swing retired | 7-45, TL08x SLOS081O p.14 |

Consequences: the buffer pull-down limits pin 8 to about -2.23 V
(750 uA x (8.1k || 4.7k)) and pin 1 to about -3.04 V. The clipping is
asymmetric (class A), not a symmetric tanh.

## 3. Numerics and runtime

* Trapezoidal (A-stable) implicit step. Newton runs on (V3, V6, Vnode1) with an
  analytic 3x3 Jacobian (Cramer). V4/V7 come from explicit trapezoid relations.
  It uses a linear predictor and a backtracking line search (at most 6 halvings),
  with a budget of 16 evaluations per sample. It converges at |e| < 1e-12 or a
  step < 1e-8 V. Non-convergence keeps the best point and counts a fallback.
  Every bench and probe run reports 0 fallbacks.
* Discretization limit: the faster section's natural frequency is clamped to
  0.25 fs. That is FCV < 0.55 V at 96 kHz and < 0.09 V at 192 kHz (about
  44 kHz chip cutoff at FCV 0). gm is prewarped on the section-1 natural
  frequency.
* Rates: 96k (factor 2) and 192k (factor 4) on the DSP worker, using the same
  decimation as the vendor model.
* Host level: superseded by docs/OUTPUT_STAGE.md. All models now run at real
  pin-8 levels through the modelled 7-52 mix amplifier (1 V at MIX OUT = 0 dBFS, S3528 ON and
  OFF). The 0.10966 matching attenuation of bab6f4a and the S3528-only 0.47
  match are removed. The S3528 input stage (-2.128) applies in both states.
* Filter Service group 8 "SSM2045 model 2 (chip)": die temperature, noise,
  offsets, OTA/Q/VCA attenuation, tail, expo, sink current (the swing row is
  retired and hidden).
  Rows that model 2 ignores are hidden. Profile schema `=4` and Filter Service
  GLOBAL=3 add a `[SSM2045M2]` section. Older states/profiles (=1/=2/=3,
  GLOBAL 1/2) still load.

## 4. SPICE reference (own, independent of GLM)

`validation/ssm2045-model2-2026-10-29/spice/ssm2045_m2.lib` is a transistor-level
macromodel of Fig. 1. It has bipolar diff-pair OTAs behind an ideal attenuator,
an NPN expo pair, the G_Q pair fed by a PNP, class-A emitter followers with a
750 uA sink, VCA cells via 450 R + PNP, and clamp diodes 1.5 V inside the rails.
The simulator supplies Vt, PTAT and shot noise. `eii_board.inc` models 7-45
plus the 7-52 -BIAS network (1N914, Vishay Fig. 1 fit IS 1.99n N 1.752,
-0.26705 V). Runs used ngspice 42 (Linux) and ngspice 47 (Windows,
F:\tools\ngspice-47).

## 5. Validation (C++ model 2 vs datasheet and vs own SPICE)

Logs: `cmp_all.log`, `eii_families_spice_vs_cpp192k.csv`,
`fig_eii_fc_q_families.png`, `spice/*.out`.

| Test | Datasheet | SPICE | C++ model 2 |
|---|---|---|---|
| Fig. 2 f3 at V5 = 0, 25 C | 632.5 Hz anchor | 633.14 Hz | -3.015 dB at SPICE f3 |
| Expo slope 25 C | -19.3 mV/oct | 19.28 mV/oct, 25.1 Hz-16.1 kHz over +/-90 mV | max -3 dB error 0.042 dB over V5 +/-90 mV x T 0/25/50/75 C |
| Temperature | PTAT (expo is a Vt law) | 20.9 mV/oct at 50 C | same law, err <= 0.028 dB |
| THD 150 mVpp, Fc/4 | 0.1 % typ | 0.1031 % | 0.1035 % |
| THD 150 mVpp, Fc | 1.0 % typ | 0.9663 % | 0.9660 % |
| THD 600 mVpp, Fc | (300 mVpp max) | 6.759 % | 6.752 % |
| Osc. onset, Fig. 2 | 150 uA | 150 uA (calibration) | 150 uA |
| Osc. 152/160/185 uA | sine | 0.146/0.325/0.598 Vpp | within 0.3 % |
| In-band loss at onset | "6 dB" | 10.7 dB | 10.8 dB (see 7) |
| S/N | 84 dB | 92 dB (shot noise only) | 84.3 dB |
| VCA THD 40 mVpp | 0.3 % typ | 0.220 % | 0.221 % |
| EII f3, FCV 0..5 V | - | 44.06k, 20.69k, 9.71k, 4.56k, 2.14k, 1006, 472, 222, 104, 48.9, 22.9 Hz | <= 0.11 dB at FCV <= 4 |
| EII in-band gain | -8.1k/68k | -18.480 dB | -18.480 dB |
| EII Q family g0, QCV 0..2.6 | - | -18.480 ... -25.865 dB | within 0.004 dB |
| EII osc. onset | - | QCV 2.70-2.72 V (86.5 uA) | same |
| EII osc. QCV 2.75/2.9/3.5/5 | - | 0.204/0.469/0.985/1.729 Vpp, 1118/1101/1053/927 Hz | 0.204/0.469/0.985/1.729 Vpp, 1111/1099/1052/926 Hz |
| EII VCA I15 vs ACV | - | 62.9 uA @1 V, 463.8 uA @5 V | within 0.1 % |
| EII full drive Y = 13.5 V | - | pin 8 +/-1.608 V | rms within 0.03 % |
| VCA1 -> out crossfeed / closed VCA2 | - | -161 dB / -144 dB below open | no crossfeed path (ideal cells); offsets option: 41.5 dB control rejection |
| Runtime families at 192k | - | 162 points | rms 0.137 dB, max 0.78 dB (deep stopband, trapezoid warping) |

Known measurement artefacts and divergences:
* FCV 4.5/5 V (49/23 Hz): the C++ lab window is too short for the settling
  time, so the "+1.6 dB" in cmp_all.log is a measurement artefact. The SPICE AC
  and the 192k families agree.
* Gross overload at Fc/4 (600 mVpp and up, at least 4x the datasheet typ.):
  SPICE 23-61 % THD vs C++ 12-16 %. The SPICE emitter-follower plus clamp diodes
  and the C++ softplus knee clip differently in that region. At Fc the two agree
  to 0.01 %.

## 6. CPU cost (Windows, desktop, 8 voices, `ssm2_cpu_bench`, O3/LTO)

| Model | 96k | 192k |
|---|---|---|
| SSM2045 vendor | 0.242 CPU s per audio s | 0.430 |
| SSM2045 model 2 | 0.352 (2.84 Newton evals/sample) | 0.602 (2.51) |
| Legacy+Q | 0.094 | - |
| EIII CEM | 0.133 | - |

Model 2 costs about 1.45x the vendor model per voice: about 0.46 us/sample at
96k and about 0.39 us/sample at 192k (vendor 0.32 / 0.28). Both run with 0 fallbacks. A Linux
container measured 0.673/1.152 vs 0.654/1.175 for the vendor model.

## 7. Disagreements with the GLM reference

1. Pin levels: GLM uses y = x/10 at pins 1/8. Fig. 1 buffers the capacitor
   voltages directly, so the real levels are 20 dB higher. This matters for
   the 750 uA sink limit and the S3528 input.
2. SIG IN: GLM treats pin 2 as a voltage input. In Fig. 1 it is a summing node
   with 900 R, 8.1k and the G_Q output.
3. Q feedback: GLM injects Q through fitted W1/W8 taps chosen to give the
   datasheet's "6 dB" loss at onset. Fig. 1 shows G_Q between the node and
   pin 8 and nothing else. With that topology both SPICE and C++ give 10.7-10.8
   dB at the Fig. 2 onset (12.3 dB without the node term). Model 2 follows the
   drawing. The "6 dB" text is left as an open question (rounded, a different
   test condition, or a simplified drawing).
4. Nonlinearity: GLM uses VL_G1 0.145 / VL_G2 0.26. A single attenuation
   a = 0.433 (VL about 119 mV) reproduces both PDF2 THD figures
   (0.103 % / 0.966 %).
5. Output limiting: GLM uses a symmetric tanh with VL_T 8.5 V. Model 2 uses
   the class-A 750 uA sink (PDF2), which makes clipping asymmetric.
6. Temperature: GLM uses ETA / self-heating. Model 2 uses a PTAT Vt law (the
   SPICE BJTs agree). Self-heating is not in the datasheet. It is a hypothesis
   and is not modelled.
7. VCA: GLM VL 0.211 gives 0.075 % THD at 40 mVpp. Model 2 gives 0.22 %, near
   the datasheet's 0.3 % typ.
8. Q pin: GLM has an I-V curve fit. Model 2 uses a PNP law with 1.8k (Fig. 1)
   plus board R146 22k.
9. Noise: GLM uses a resistor noise trick. Model 2 is calibrated to the 84 dB
   S/N. A SPICE run with shot noise alone gives 92 dB, which shows the datasheet
   includes more than shot noise.
10. Convergence: GLM `tb_ac.cir` reaches no operating point in ngspice 42 or 47.
    The log shows a singular matrix at x1.tnoi2 and "timestep too small". Its
    numbers could not be reproduced (`glm_tb_ac_ngspice47_excerpt.txt`).

## 8. Real-bank probe (firmware capture, 7.5 s, 18 Grand Piano #2 / 47 Big Brass & Cymbal)

`probe_gp.log`, `probe_brass.log`:
* Native route (Hardware DAC + deglitch front-end ON): model 2 rms is within
  0.5 dB of the vendor SSM (piano 0.00409 vs 0.00435, brass 0.00935 vs 0.00993).
  0 fallbacks.
* Reference route (default, deglitch "nominal -50, dynamics bypassed"): the
  deglitch output Y reaches |Y| = 49.7 V (piano) and 227 V (brass). 0.4 % and
  4.1 % of samples are beyond the 13.5 V TL084 swing. The vendor models ignore
  rails. Model 2 limits Y at the swing, so on this route it compresses (rms
  -2.5 dB piano, -8.6 dB brass vs vendor). That clipping comes from the
  reference-route input level, not from the chip. It is the open item #4 of
  ANALOG_AUDIT_S3528_SSM2045.md.
* Resolved in docs/OUTPUT_STAGE.md. The no-DAC route is level-matched at
  1 kHz, and every model has the TL084, S3528 and MC34004 rails. The "model 2
  quieter" report came from the owner's Filter Service trims on the old
  models. The real defect was that the whole output sat 25.76 dB below the
  board.

## 9. Open questions

* Datasheet "6 dB" at onset vs 10.8 dB from the drawn topology.
* Resolved: the hardware DAC route is the faithful one (default); OFF = no DAC,
  raw digital codes, level-matched at 1 kHz (docs/OUTPUT_STAGE.md).
* Die temperature and self-heating in an EII enclosure (Ssm2DieTemp is exposed;
  25 C default).
* Noise spectrum shape (white only; no 1/f data in the datasheet).
