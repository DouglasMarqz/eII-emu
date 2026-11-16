# Basic host audio and instrumentation — 0.37 preview

## Compile out optional instruments

`EII_DIAGNOSTICS=0` removes CPU fetch/port histograms, trace callback calls,
hot-PC sorting and full periodic GUI model-report generation. Excluded code
cannot be enabled by a runtime checkbox. Raw compiler builds default OFF with
NDEBUG, ON otherwise; explicit `-DEII_DIAGNOSTICS=0/1` overrides that. CMake
defaults `EII_ENABLE_DIAGNOSTICS=OFF`; set ON for diagnostic tools. PowerShell
build-win64.ps1 defaults OFF; pass `-Diagnostics` for ON. The Diagnostic EPROM
checkbox selects firmware, not instrumentation.

CPU WAIT, memory known masks and device/controller state are execution state,
not optional instruments. Some component counters remain. Only a clean ON/OFF
measurement can establish what removing these instruments saves; previous
measurements overlapping builds/tests must not be used for that conclusion.

## Host stream and basic voice

Windows GUI starts an event-driven WASAPI shared render stream on the selected
endpoint: 48kHz stereo 32-bit float, with Windows shared-mode format conversion.
A dedicated thread owns COM and sleeps on render/stop events. Stop joins before
changing devices. Startup/device errors appear in audio status. No test oscillator
or polling loop is in the application.

The machine worker supplies RuntimeAudioEngine blocks from actual held DAC codes
and firmware ACV/FCV holds. A rational CPU-boundary scheduler emits48000 frames
per4million CPU ticks. It does not enable the20MHz laboratory analog observer,
look ahead into sample RAM, or alter CPU/WAIT/DRAM/DAC state.

The default BasicVoice route connects existing components:

1. ACV reference RC (2.35ms) and Am6072 steady DAC transfer.
2. R138/internal-input attenuation900/68900.
3. Existing SSM2045OpenLoop four-pole small-signal filter with EII C1..C4.
4. C93 coupling, R74/R82 divider470/10470.
5. Existing Ssm2045LinearVca and R154/C161 feedback kernel.

To get a functional first voice without waiting for physical calibration, the
**provisional absolute anchor** is678.82251Hz at FCV2.5V. As corrected in0.40,
the FCV slope/direction follows the7-45 R122/R130/R112 divider and the
manufacturer's typical -19.3mV/octave pin5 sensitivity. Positive FCV closes the
filter. The old increasing30..15360Hz map was reversed. The absolute anchor
is still not measured board calibration or a calibrated -3dB cutoff.
Coefficients change only when firmware changes FCV. The VCA uses a fixed500uA
branch-current initial setting; the real mixer/bias distribution remains to be
modeled. The amplitude envelope is already carried by the real DAC ACV hold;
it is not multiplied twice as a fabricated extra envelope.

This initial filter is zero-Q-current: resonance/QCV feedback is pending.
3202 switching and S3528 reconstruction are bypassed in the basic runtime route.
Their existing reference/laboratory models remain intact. No production
antialiasing, nonlinear clipping or calibrated analog-output claim is made.
Unknown DAC/ACV/FCV mutes/resets that host voice; known input recovers from explicit
zero analog initial state. Channel0 is admitted only when mainPIOB7 is a known low playbackMODE output
(0.38); sampling/input/unknownMODE remains muted.
The seven other channel outputs are summed as volts into a1V host full-scale
reference, duplicated to stereo and host-limited to[-1,1]. This headroom/limiter
is not a model of the physical output-board clipping law.

A raw DacMonitor mode remains available to probes for isolating the DAC route.
It uses1/40 summing headroom; the GUI uses BasicVoice.

## Transport and practical limits

The8192-frame mutex-protected queue bounds memory and latency. Producer pushes
small blocks, never waits for device demand. Overflow drops oldest frames;
underflow renders silence. Pause/reset/error clears queued audio. If emulation
cannot supply real-time frames, gaps are expected. Boot idle is normally silent;
MIDI IN short-message input is connected in0.38; see MIDI_INPUT.md.

Native tests cover rational frame counts, DAC polarity, CV/mask/channel0 guards,
low/high FCV attenuation, stability, zero-ACV mute and unknown recovery; bounded
FIFO/drop/underflow/reset and concurrent transport. The real-Z80 transport fixture
proves OUT -> RF -> ALU -> DRAM -> IC40 -> ACK -> DAC/ACV/FCV -> VCF/VCA -> nonzero
stereo PCM. Synthetic test firmware is not included in the application.
Diagnostic ON/OFF tests retain actual firmware memory/DAC side effects while
histogram/trace activity disappears. Native/sanitizer, original-bank and clean
ON/OFF measurement results are recorded in validation/runtime-037-*.

LLVM-MinGW20261006 UCRT x86-64 cross-builds the GUI with O3/LTO/NDEBUG and
instruments OFF. No native Windows device playback/listening/persistence or CPU
measurement was performed here. Cross-compilation does not establish audibility
or device lifecycle. Windows checks: select/change endpoint, boot media, pause,
reset, remove device, close, restore preferences and trigger a real firmware voice
when note input is available. Detailed analog refinement will follow the service
manual; this basic VCF/VCA connection is deliberately usable before that work.

## Clean engine instrumentation comparison

Sequential ABBA,24M CPU ticks (6 simulated seconds), same O3/NDEBUG/LTO source
and flags except EII_DIAGNOSTICS, audio disabled, no overlapping builds/tests:
ON10.3312/10.3740s, OFF10.5355/10.6901s. Means ON10.3526s, OFF10.6128s; this run
shows no throughput improvement from removing these instruments (OFF0.565x
real time). This small paired container experiment is not a Windows CPU metric
or a basis for declaring every remaining diagnostic cost harmless. It measures
the engine; periodic GUI reports are not generated by this headless benchmark.
All complete reports,64RF values/masks and memory hashes match after removing
only the compiled-out PC/IO histogram lines. Raw samples are in
validation/runtime-037-clean-diagnostics.json.

## 0.38 real MIDI result

Superseded by the0.39 test-setup correction below; retained as checkpoint history.

Actual-bank MIDI now reaches the real firmware and nonzero ACV, but PCM peak
remainszero. The external end-to-end audio probe fails; synthetic component
passes do not establish audible original-bank playback. See MIDI_INPUT.md and
validation/runtime-038-original-midi-diagnostic*.

## 0.39 correction and release-tail CPU fix

The0.38 MIDI playback probe left its selected controller in reset. The GUI did
not. With shared constructor initialization, the original-bank probe now passes
the real firmware/MIDI/DRAM/DAC/basicVCF/VCA-to-PCM check (peak0.000163143820828).
See validation/runtime-039-original-midi.txt; this is a measured PCM result,
not a native Windows listening/device-routing test or analog calibration.

The host PCM path now scopes x64 FTZ/DAZ around its DSP. A released eight-voice
tail had previously settled into persistent subnormal arithmetic while emitting
zero float PCM. See RUNTIME_039_BUG_AUDIT.md and the recorded before/after tests.
Source-clock controller cost and the user's first-output selection issue remain
unresolved; the latter is independent of emulated voice0.

## 0.40 functional host-path corrections

User confirms OUT01–02 on the UMC1820 is selectable; endpoint selection is no
longer the reported blocker. The original-bank signal before correction was
continuous but about-89dBFS RMS, so the old nonzero-PCM assertion was insufficient
to establish useful output level. The FCV direction was wrong and the unity-wire
deglitch bypass omitted its nominal low-frequency inversion/gain -R34/R26=-50.
The basic path now retains that nominal gain; switching dynamics, rail behavior
and physical calibration remain absent. No extra host normalization was added.

See HOST_AUDIO_040.md and validation/runtime-040-* for circuit evidence, exact
provisional boundaries and original-firmware PCM statistics. The probe optionally
exports its actual postboot48kHz stereo PCM as16-bit WAV without normalization.
