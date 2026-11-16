# Filter service —0.55

Open **Filter service...** on the firmware panel. This modeless window leaves the main panel, MIDI, wheels and scanner sliders usable. Calibration affects the nonlinear96/192k engine only; Legacy is unchanged. One global profile applies to all eight chip voices. S3528 remains bypassed; these controls cannot calibrate an absent reconstruction stage.

## Workflow

1. Select nonlinear96k initially and open Filter service. **Low-Q start** changes only Qscale to0.60, master trim to0.25 and disables manualQ override. It is an audition starting point, not a measured calibration.
2. Use normal firmware controls for cutoff/Q, or explicitly enable the lab pin overrides. Tune sliders; exact numeric edits apply on Tab or focus loss. **Reset all** restores0.54 analog defaults.
3. Valid changes apply at the next DSP block boundary without resetting integrators or restarting the firmware. Master gain ramps; optionalQ smoothing is adjustable. Overload can delay application; applied revision is visible.
4. Values auto-save after500ms inactivity to the existing per-user settings. Save also writes immediately. Export/Import save and restore complete versioned text files. Import validates every key/range and rejects incomplete, duplicate, nonfinite or unsupported profiles atomically.
5. **Copy profile** copies all exact values for pasting into the conversation. Capture state also includes the profile and DSP counters. ROMs, disk media and firmware bank edits are not included.

The VCA pin15+pin16 control-current total is constrained to500uA; moving one slider lowers the other if needed. Numerical fields use a decimal point. Boolean fields are0/1. Offset values are inactive until Enable chip offsets is1.

## Resonance

The150uA value is a normalization for the upstream Figure2 circuit. Linearizing the actual equations with EII20n/3.9n/20n/1.8n capacitors yields a small-signal oscillation boundary near73.410uA, versus149.99996uA with the Figure2 capacitor set. At the provisional22kOhm/+0.6V input mapping this isQCV≈2.215V. This is a model calculation, not measured EII calibration, and does not establish an exact correspondence with a particular firmware slider value. Qscale/Q input resistance/emitter offset/Q normalization allow explicit adjustment. No silent hardcoded correction was substituted.

The present adapter applies amplitudeCV to the DAC reference, with a provisional constant VCA bias. Self-oscillation can therefore persist independently of a released sample. **VCA follows ACV (lab)** is an explicitly experimental alternative; it defaults off pending board calibration.

## DSP stability and diagnostics

0.54 reset all eight filter states whenever the bounded DSP queue dropped a block.0.55 preserves those states and crossfades the first64 output frames after a gap. This removes the reset transient, but cannot reconstruct dropped input time or guarantee a CPU deadline. DSP load is processing time/audio duration for a block, smoothed; it is not overall system CPU or machine speed. Queue/dropped/failure/solver-fallback/iteration counters are visible only in the open service panel and in manual capture.

The implicit equations,12-iteration Newton bound and tolerance are unchanged. Residual and Jacobian share nonlinear evaluations; an exact sparse4x4 solve replaces generic elimination on the known matrix, retaining generic pivoting for a near-singular determinant. A48,000-sample comparison against a frozen0.54 core passes with maximum absolute error5.65e-15 over nonlinear cases and stable linear cases. Above-onset linear configurations are intentionally unbounded and excluded from that waveform-comparison claim.

Indicative native single-voice0.1s benchmarks at192k/FCV0/QCV3.125:0.03431s old versus0.02241s new (~35% less time); all16 nominal FC/Q benchmark cases had zero solver fallbacks. Eight-voice192k worst cases still exceed realtime on this execution host. Target Windows speed/headroom is unverified; no100% guarantee. The separate DSP worker,48k host output and16ms bounded DSP queue remain. Existing mix clipping is adjustable through master trim, not silently normalized.

## Validation

- profile roundtrip, boundary ranges, corrupt/duplicate/incomplete input rejection, totalVCA current, live trim and revision:PASS
- deliberately stalled DSP consumer: bounded drops and continuation of pre-gap filter state compared to an uninterrupted reference:PASS
- AddressSanitizer/UBSan for service/profile/queue tests:PASS; leak detector disabled for the host /proc limitation
- chip/audio worker, upstream VCF+VCA, Legacy runtime audio, synthetic Z80 front-panel session and live calibration without firmware advance:PASS
- Win64 Release cross-build uses-Wall-Wextra-Werror; target GUI, clipboard/dialog and registry behavior still require Windows runtime validation

## Parameters

|Group|Key|Meaning|Unit|Range|Default|
|---|---|---|---|---|---|
|0|`FcRin`|FC input R|kOhm|1…100|22.6|
|0|`FcRbias`|FC bias R|kOhm|10…1000|124|
|0|`FcRshunt`|FC shunt R|kOhm|.1…10|1|
|0|`FcSupply`|FC bias supply|V|-20…0|-15|
|0|`FcCap`|FC smoothing C|uF|0…20|3.3|
|0|`Qres`|Q input R|kOhm|1…200|22|
|0|`Qemitter`|Q emitter offset|V|0…1.2|.6|
|0|`Qscale`|Q current scale|x|0…2|1|
|0|`Qmax`|Q current ceiling|uA|0…300|300|
|0|`Qtau`|Q current smoothing|ms|0…100|0|
|1|`C1`|C1|nF|.1…100|20|
|1|`C2`|C2|nF|.1…100|3.9|
|1|`C3`|C3|nF|.1…100|20|
|1|`C4`|C4|nF|.1…100|1.8|
|1|`Gm0`|gm reference|uS|1…500|72.188|
|1|`Vtef`|FC exponential scale|mV|10…60|27.844|
|1|`FcBias`|FC chip bias|mV|-20…20|.982|
|1|`Qonset`|Q normalization current|uA|50…300|150|
|1|`W1`|Q feedback W1|x|0…6|2.10216|
|1|`W8`|Q feedback W8|x|0…8|3.10216|
|1|`GaLimit`|Ga soft limit|V|.1…5|1.2|
|1|`G1Limit`|OTA G1 soft limit|V|.02…1|.145|
|1|`G2Limit`|OTA G2 soft limit|V|.02…1|.26|
|1|`TLimit`|Output soft limit|V|1…15|7|
|1|`Nonlinear`|Nonlinear OTAs|0/1|0…1|1|
|2|`Offsets`|Enable chip offsets|0/1|0…1|0|
|2|`Vos1`|VOS G1|mV|-30…30|4|
|2|`Vos2`|VOS G2|mV|-30…30|-11|
|2|`Vos3`|VOS G3|mV|-30…30|6|
|2|`Vos4`|VOS G4|mV|-30…30|-9|
|2|`Ios1`|IOS N1|nA|-100…100|12|
|2|`Ios2`|IOS N2|nA|-100…100|-9|
|2|`Ios3`|IOS N3|nA|-100…100|15|
|2|`Ios4`|IOS N4|nA|-100…100|-11|
|3|`VcaGain`|VCA gain|A/V/A|1…20|8.17|
|3|`VcaRange`|VCA soft limit|V|.02…1|.211|
|3|`I16`|VCA pin16 current|uA|0…500|0|
|3|`I15`|VCA pin15 current|uA|0…500|500|
|3|`Expo`|VCA EXPO voltage|V|-.25….25|0|
|3|`VcaFloor`|EXPO floor|x|0….05|.001|
|3|`VcaVt`|EXPO thermal scale|mV|10…60|25.85|
|3|`VcaOffset`|VCA output offset|uA|-20…20|0|
|3|`InputGain`|Pre-filter input trim|x|.01…4|1|
|3|`OutputGain`|Post-VCA trim|x|.01…4|1|
|3|`MixGain`|Host master trim|x|.01…2|1|
|3|`ReferenceTau`|DAC reference smoothing|ms|.01…20|2.35|
|3|`CouplingTau`|VCA coupling time|ms|.1…100|10.47|
|3|`VcaFollow`|VCA follows ACV (lab)|0/1|0…1|0|
|4|`FcOverride`|Override FC pin|0/1|0…1|0|
|4|`FcManual`|Manual FC pin|mV|-130…130|0|
|4|`QOverride`|Override Q current|0/1|0…1|0|
|4|`QManual`|Manual Q current|uA|0…300|0|
|4|`AcvOverride`|Override amplitude CV|0/1|0…1|0|
|4|`AcvManual`|Manual amplitude CV|V|0…5|0|
