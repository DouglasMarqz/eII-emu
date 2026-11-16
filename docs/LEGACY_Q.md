# Legacy + Q — VST3 0.2.1 test candidate

Baseline: owner-accepted VST3 0.1.2 mini golden, private feature/vst3 e455f253c2f91994b89afeb8bad56a9e8dff155d. This candidate is separate and not owner-accepted/committed/pushed. The existing Legacy zero-Q and nonlinear SSM96/192 modes retain their indices. Legacy + Q is model index 3 (RuntimeAudioMode::LegacyQ appended after existing enum values). Standalone GUI does not expose this new model in this milestone.

## Signal path / timing

Native hardware events -> existing NativeCollector / NativeDacChannel (AM6072 reference, I/V and switched deglitch with integrated area) -> new LegacyQFilter -> existing optional S3528 reconstruction -> existing coupling -> selected VCA -> existing feedback -> existing decimation/DC/monitor trim -> 48k transport -> host resampler.

There is one DAC/deglitch stage. The reference route remains selectable by switching Native DAC off; it uses the existing simplified signed DAC/reference route. Choosing Legacy + Q in Settings enables Native DAC and S3528 by default; both remain visible switches.

The CPU/firmware/divider/sample-address clocks are unchanged. Native timestamps retain the 1/120 MHz rational grid (6 units per hardware half-edge); 2500 units per transport frame. This is not an assertion that the sampler hardware samples at 48k: its changing voice/sample timing remains in the hardware events. 96k is the VCF numerical solver grid, 48k is the decimated plugin transport, and host rate conversion stays downstream.

## Fast VCF / Q

The two original linear two-pole sections are solved with closed-form trapezoidal state updates. Ga feedback uses the existing SSM board/core parameters: Q current from firmware CV, Qres/Qemitter/Qscale/Qmax/Qonset and W1/W8; bounded feedback ga = aq * VL_Ga * tanh((W1*y1 + W8*y2)/VL_Ga). Only this one scalar feedback value uses safeguarded Newton/bracketing. No four-state nonlinear Jacobian solve is used. The feedback is bounded without clamping firmware Q to a sub-oscillation ceiling. Zero input has no invented thermal-noise seed.

This is a fast approximation, not a hardware-equivalent SSM2045. OTA saturation, output tap saturation and chip offsets are omitted from this VCF; their controls and offset group are hidden in Legacy + Q Filter Service. DAC/deglitch/S3528 retain their shared existing implementations. Legacy now uses its own linear VCA; SSM uses its nonlinear VCA. At large signals/high Q, its sound/self-oscillation amplitude differs from the full nonlinear SSM. Nominal parameter values are shared defaults, not newly measured unit calibration. No audible equivalence claim.

## Parameters / state

Filter Service edits a separate legacyCalibration profile. The existing SSM calibration field is untouched. Settings serialization adds a validated legacycalibration vector before the original calibration section; old states without it use nominal defaults. Import/reset/export operates on the currently opened model profile. Changing model closes the service view so controls cannot silently refer to the previous model. Plugin state includes both profiles; the plugin class ID is unchanged. Do not save an important golden project over itself while testing a new model.

## Verification / limitations

See validation/legacy-q-2026-10-23.md. Zero-Q is checked against the original open-loop trapezoidal kernel; subcritical small-signal Q against the independent four-state SSM solver with nonlinearities disabled. A supercritical linear oracle is intentionally unsuitable for the bounded large-signal model because the former grows without saturation. Default FC/Q stress, independent existing deglitch RK4/time-grid suite, separate state/profile tests and frozen mini-golden SSM output comparisons are included.

Native Windows host playback, audible balance/resonance, live controls, editor lifecycle and project restoration still require owner testing. Digital-controller optimization is outside this patch; faster DSP is not a promise of the same factor for the complete emulator.

## Board FC/Q versus chip FC/Q — explicit separation

Board FC/Q is the external resistor/bias/RC network that maps firmware control voltages to chip FC/Q pin conditions. Chip FC/Q is internal gm/exponential/Q-feedback tuning (and chip capacitors). These remain distinct groups with unchanged original SSM keys/defaults/loaded values. Each model profile keeps these groups separate; editing Legacy never overwrites the SSM profile. Reset group resets exactly its selected group, preserving the other groups.

0.2.1 shows complete exclusive VCF/VCA models in the model menu, with common DAC/deglitch and S3528 switches in a separate stage menu. SSM96/192 are solver rates of the same nonlinear model/profile. Service title/header identifies the model and refuses stale model edits.

0.2.1 Legacy VCA is linear, with the original small-signal current law and nominal4085uS gain at500uA; full SSM VCA includes its audio saturation. The former0.2.0 hybrid shared VCA is superseded. See validation/vst3-021-model-separation-2026-10-23.md for new tests and limits.
