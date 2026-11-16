# Requested follow-up work

## Windows ASIO output — requested2026-10-19

Add an optional ASIO backend alongside WASAPI, with driver enumeration and
selection, supported sample-rate and buffer controls, and persisted choices.
Validate against the user's UMC device under Windows: note-to-audio latency,
stable playback, driver stop/start and sample format handling. Retain WASAPI
as a selectable alternative. This is planned work, not an implemented0.45 option.
Check the current official SDK/distribution requirements when implementing it.

## Host audio queue and remaining headroom

0.49 implements a20ms default FIFO bound, raised to hold one reported WASAPI
buffer if larger; manual capture reports limit/highwater. Original-bank dense
input retains bit-exact PCM/memory when production observation mirrors publish
at block boundaries. Windows96% under load and the user's zero-overrun stuck
sound still require real input capture/retest. Device clock drift, underflows
and remaining engine headroom stay open. Do not equate the
paced speed meter with spare CPU capacity or conceal sustained slow emulation.
Keep per-packet full diagnostics disabled.

## SSM2045 resonance and S3528 production audio

Still pending after0.50 sample-address checkpoint. BasicPlaybackVoice is a zero-Q
transfer; S3528 is bypassed. Derive/solve the actual coupled SSM2045 Q loop with
TPT/ZDF state continuity and board QCV input current, validate range and
nonlinear/oscillation behavior without calling an uncalibrated transfer exact.
Adapt the existing AMI typical S3528 amplitude/group-delay fit for host-rate
rendering with cached parameters. Do not connect the source-edge laboratory
analog observer as the normal Release audio engine. See REALTIME_INPUT_049.md.

## Reverse playback address correction — 0.50

The shared UCD04.1A ALU addend wiring was corrected in both engines after a
schematic re-audit. The original-bank preset02 single-note reproduction no
longer walks startup DRAM or clips; preset01 capture is unchanged. This is a
digital-path fix, not a calibrated analog improvement. See SAMPLE_ADDRESS_050.md.
