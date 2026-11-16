# S3528 audible-path integration: verified starting material

Implementation follow-up: [0.69 runtime](S3528_RUNTIME_069.md).
The inventory below records the pre-integration0.68 state.

2026-10-21, release baseline0.68. User requested starting the missing
reconstruction stage and verifying whether the service manual/datasheet remain.

## Available primary sources

Local EII service manual:151 PDF pages,100330051 bytes, SHA256
f1071d347818ecfd107df0808ceade6d9e460987a85f86a04c71707a82700031.
Local user-supplied63928.pdf:11 pages,844220 bytes, SHA256
6f6deb600c1547e1b5be41b9a35b81eb68db51ac3c38e694f4653003e7e47c9e.
It is AMI S3528B February1993,10 manufacturer pages plus a distributor page.
Both PDFs were found in local storage; their Library filename identities were
also resolved. Originals remain outsidegit. No new upload is needed to begin.

The previously cropped channel1 schematic has been visually reviewed again:
SSM2045 four-pole output -> S3528 external input network -> FILT pin9 ->
C93/R74/R82 -> SSM2045 VCA. R154/C161 are at the output feedback amplifier.
Datasheet front page and printed51 notch text were visually reviewed again.
A1993B typical-curve fit does not prove individual1984 EII silicon calibration.
See docs/S3528_CURVE_FIT.md for exact provenance and model error budgets.

## Existing code verified

- Machine::output decodes ports40h..47h into8 S3528Control instances.
- S3528Control implements6-bit programming and64 divisors; cutoff law is
  master/(40*N), currently5MHz nominal board clock.
- Machine::playbackSwitchInputs already exposes code/cutoff, but only the
  separate clocked laboratory path consumes them.
- S3528NominalResponse and fitted SOS coefficients exist, with executing
  sinusoidal gain/delay validation against held-out manufacturer plot points.
- Both audible BasicPlaybackVoice and NonlinearPlaybackVoice bypass S3528.
- The DSP worker currently receives codes/ACV/FCV/QCV/channel0 mode but does
  not receive reconstruction control codes. Wiring that boundary is required.

## Concrete implementation order

1. Verify the complete board5MHz/CE/program-bus wiring against the service
   sheet and compare firmware-emitted codes during bank/Play operation.
2. Add immutable per-voice6-bit control values to the existing DSP input
   packets, preserving knownness and existing digital write timing.
3. Choose and validate a fast runtime reconstruction method. Existing SOS
   coefficients run at40*fc, with a pair-average stage at80*fc. Running those
   coefficients unchanged at96/192k would give the wrong cutoff. The nominal
   chip clock can exceed renderer rate; code/rate conversion and treatment of
   prefiter/hold behavior must be explicit. Do not add5MHz per-edge work to
   the machine owner or silently clamp control codes.
4. Insert after VCF and before the C93 coupling/VCA, keeping existing branch
   gain/feedback conventions and all source assumptions visible. Preserve a
   switchable bypass for accepted0.68 sound and comparison; expose active
   state/codes in trace so tests establish actual firmware control.
5. Validate all64 codes, rate/knownness changes, DC/gain/delay/stopband,
   changing-code transients, renderer stability and cost at96/192k; compare
   bypass output and full digital state to0.68. Run original bank/Play stress.

No physical EII is required for this documented nominal integration. Physical
capture would be needed to claim unit-specific calibration, noise/feedthrough,
rail behavior or true divider restart phase. The current source does not
justify inventing those effects. Deglitch3202/TL084 switched timing remains a
separate omitted runtime stage, and should not be silently conflated here.

This is the readiness/wiring checkpoint, not a new integrated filter release.
