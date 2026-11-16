# 0.44 MIDI wakeup pacing and WASAPI buffering

User0.43 report: near100% speed at rest, sometimes104%, falling to93% with
busy keyboard play, and excessive key-to-sound delay. The two pasted captures
are identical (1458080000 ticks). Production invalid0, DMA supported1/disabled,
no grant, active DAC/ACV values; this is not evidence of the previous blocked
unsupported-DMA state. One capture cannot show queue growth or CPU cost.

## Reproduced host scheduling fault

The worker executes a20k-tick (5ms) quantum, then waits to its wall-clock
anchor. A MIDI command wakes that wait early. Previously the next loop applied
the command and unconditionally executed another quantum, even when simulation
was already ahead. Repeated callbacks could therefore grow future audio and
consume work before it was due. Every MIDI also forced snapshot/report copying.

An asset-free same-source paced worker burst for150ms gives:

* 0.43:562 messages,800000 ticks in0.151278s,132.207% progress; about49ms ahead.
* 0.44:539 messages,600000 ticks in0.151108s,99.2665% progress.

The stress deliberately exceeds physical DIN throughput. It isolates host
wakeup pacing; it is not a measurement of a musical keyboard's serial rate.
The permanent regression in front_panel_session_tests uses a time-bound burst,
checks progress stays within two quanta of wall time, bounds snapshot count,
and verifies on-demand queue capture. It remains valid on slower CI machines.

The worker now applies commands promptly but runs no extra quantum before the
current wall deadline. MIDI counters/snapshots use the usual100ms publication
cadence. Pause, boot, capture and other controls still publish promptly;
explicit Step and unpaced execution are unchanged. Firmware and UART31250baud
serialization, analog DSP, production controller and DMA behavior are unchanged.

## Audio endpoint buffer

The event-driven shared stream previously requested1000000 100ns units (100ms).
Microsoft IAudioClient::Initialize documentation requires both durations0 for
shared event-driven streams. The driver now chooses the engine's minimum
buffer; GetBufferSize reports the actual capacity shown in the UI. This does
not mean zero end-to-end latency or a guaranteed device-specific period.
Source: https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize

UI shows WASAPI buffer and host queue in ms. Manual Capture state adds host
queued/dropped/missing frames, UART counts and pending MIDI wire bytes. Missing
frame totals include boot/pause/startup silence, so use changes between captures
while running; they are not automatically evidence of an audible failure.
No continuous full report or PC/IO histogram is enabled in Release.

## CPU limit and validation

One native exploratory unpaced production/basic-audio run, same original bank:
8M CPU ticks (2 simulated seconds) take idle2.12557/2.16532s, eight held MIDI
notes2.23755/2.19794s, release2.21519s. Mean held cost is about3.4% above idle.
This is not a Windows CPU utilization measurement or a complete reproduction
of rapid user playing. Core real-time headroom still needs work; no claim that
the reported93% under dense playing is fully resolved.

Release worker/pacing/manual capture and real-Z80 UART/CTC/MIDI tests PASS.
The native benchmark tool builds warning-free. Win64 LLVM-MinGW20261006
O3/LTO/NDEBUG/DIAG0 crossbuild has zero warnings; Windows runtime untested.
The emulator core and DSP are unchanged; the0.43 bank PCM evidence remains
applicable at identical simulated ticks, without a new waveform-fidelity claim.
