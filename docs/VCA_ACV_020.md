# Custom0.3.20: downstream VCA ACV control

Owner reported maximum-resonance sound continuing with minimum ADSR and after
ALL NOTES OFF, across SSM, Legacy+Q and custom CEM. The previous adapter used
fixed downstream VCA currents by default. ACV already controlled DAC-reference
amplitude, but that cannot silence an oscillator generated in the following VCF.

## Board connection

EII service manual PDF107, output channel1 sheet7-45, visually checked at180dpi:
ACV -> R91 4.7k -> C112 1uF node -> R100 4.7k -> SSM2045 VCA2 linear pin15.
C112 returns to analog ground. This is separate from DAC-reference C177.
Earlier chat reference to R102 was a low-resolution misread; the drawing says
R100. Chip EXPO pin14 remains the separate -BIAS connection, not this ACV node.
Datasheet: https://www.synfo.nl/datasheets/SSM2045-ssmt.pdf, Figure4 uses nominal
450ohm internal input resistance and describes cutoff within about0.5V of ground.
These are nominal approximations, not a measured individual-chip calibration.

VcaBoardControl integrates the loaded capacitor analytically, including crossings
of the0.5V control threshold within one solver step. Below threshold the capacitor
time constant is4.7ms; above it is (4700||5150)*1uF =2.45736ms. Steady branch
current is max(0,(ACV-0.5)/9850), nominal456.8528uA atACV5V.
No arbitrary note gate, amplitude noise gate, VCF reset or resonance ceiling is used.

## Calibration and compatibility

The actual board current divided by500uA scales existing I15/I16 calibration
currents in both the SSM and Legacy VCA implementations; custom CEM uses the
existing SSM VCA downstream. I15 retains its old nominal500uA reference trim.
VcaFollow=0 (unchanged default) now selects the physical board approximation;
VcaFollow=1 retains the explicit experimental instantaneous ACV/5 law and is
relabeled accordingly. Both consume the existing firmware ACV, not a new ADSR.
An enabled manual ACV override can deliberately hold the VCA open.

Old projects and three-model service/global profiles remain readable, with no
schema or saved calibration-number changes. The repaired topology intentionally
changes amplitude envelopes and levels: both DACreference and final VCA follow
ACV as drawn. No automatic gain compensation is applied. This is not a claim
of bit-identical sound versus the faulty0.3.19 topology.

Sample-input gain, final host master, filter calibration, Q behavior, clocks,
ADC/ISMPL/DMA and firmware remain unchanged. Historical zero-Q BasicVoice is not
modified. This custom test does not merge into pure feature/vst3.

## Validation

The same synthetic voice test against0.3.19 reproduces the audible leak in all3
models at96/192k on both reference and native-input paths. Corrected tests close
the VCA to zero output after50ms with ACV0 and reopen to nonzero selfoscillation
with no new audio excitation. The filter is not reset at VCA closure.
Loaded-capacitor voltages agree with independent32-substep RK4 integration to
less than2.25e-10V; threshold, DC current, current bounds and reset pass.
Existing filter_service_tests, chip_audio_tests96/192 and cem_service_tests pass.
These are local synthetic tests, not Windows firmware/DAW acceptance.

Final plugin uses the pinned release compiler/backend, O3/LTO/static/DIAGNOSTICS0,
background1, original input/link order and strict profile-mismatch errors. The
compatible0.3.18 native profile is reused unchanged. The new helper uses the
original unprofiled-function policy; no mismatch suppression or recipe change.
Four existing SDK format warnings. Optional VCA-CONTROL-TEST.exe is a separate
uninstrumented diagnostic built without PGO, not the installed plugin.

Next: owner checks short ADSR/max resonance, key release and ALL NOTES OFF in
each model, repeated notes, normal presets and project/service save-load. Inspect
manual ACV override if a held sound persists. Windows runtime is pending.
