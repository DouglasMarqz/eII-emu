# MIDI IN — 0.38 preview

Update0.39: user-confirmed Windows input on VI61 (96UART bytes/96firmware reads,
0overruns/drops). The0.38 zero-PCM probe diagnosis below was invalid because its
controller remained reset; the GUI had the correct reset release. Shared phase
construction fixes the probe, and the original OS/bank now passes12bytes/12reads,
nonzero PCM (peak0.000163143820828). Historical failure logs remain available.
See RUNTIME_039_BUG_AUDIT.md; Windows listening/output-selection validation and
the low-CPU runtime work are still outstanding.

Select a port in **MIDI IN**, then boot/run the real OS and bank. **None** closes
input; **Refresh MIDI** enumerates ports again. The selected input is remembered
with the other panel preferences. The status shows host messages, UART bytes,
firmware reads, receive overruns and queue drops. **All notes off** sends MIDI
CC123 on all16 channels through the same receiver; it does not edit voices.

Windows WinMM callbacks enqueue complete short messages only. They do not access
Machine, render audio, touch HWNDs or allocate firmware voices. One worker owns
Machine; its pending command queue is bounded for MIDI (512). MidiWire serializes
bytes at1280 CPU ticks/character (4MHz /31250baud *10bits) into the main CPU
SCN2651 receive holding register. A4096-byte wire queue rejects overflowing
packets as a whole. Receiver overrun is separate and appears in chip status.

The actual route was visually checked in service sheets7-13 and7-16:
MIDI optocoupler ->SCN2651 RXD, external500kHz clock/16, RXRDY_n/SERINT ->main
CTC1 pin22 ->firmware interrupt ->CPU IN. CT2 pin21 is not this net. Manufacturer
reference: Philips SCN2651 product specification,1994-04-27, tables4..8:
https://pdf.dzsc.com/SCN/SCN2651.pdf (manufacturer document, hosted mirror).

Original firmware configures MR1=4E,MR2=00,CR=27. The register subset models
mode-register sequencing, read-command pointer reset, Rx enable/disable, ready,
holding-register reads, sticky overrun/latest received byte and error reset.
It receives valid external8N1 character-completion events, not analog optocoupler
edges or bit-level framing/parity faults. Existing diagnostic serial behavior
outside MIDI8N1 mode is retained; this is not a complete SCN2651 model.

Note-on/off (including velocity-zero on), CC/sustain, pressure, program, bend and
short system/realtime messages are transported unchanged. The firmware interprets
them. SysEx and host MIDI OUT are not implemented. Duplicate WinMM device names
use an occurrence index in the saved ID; reordering identical devices can require
reselection. Port opening/closing/device removal still need native Windows checks.
Input is closed before destroying the emulation worker. Paused input remains
queued/bounded until the machine resumes; reset replaces the old wire/receiver.

## Validation and outstanding sound failure

Full native suite passes. New test executes real Z80 UART configuration, sends
paced data and verifies CTC1/IM2/IN stores the RHR byte in memory. It also checks
mode/pointer/status, overrun/reset, disabled/unsupported receiver guards, packet
lengths and queue limits. Worker tests reject truncated/statusless packets.
UART ASan+UBSan passes with leak detection disabled; no TSAN claim.

On the original GrandPiano OS/bank, two note-ons and two note-offs produce
12 received bytes,12 firmware reads,0 overruns/rejections and14 voice-programming
commands. Actual amplitude CV reaches3.65234375V on channels0 and1.
**PCM remains zero in that run. The full MIDI-to-PCM probe therefore fails its
audio check.** This preview supplies the requested MIDI trigger and status, but
is not yet an audible original-bank sampler. See validation/runtime-038-*.

The previous unconditional channel0 host mute is also corrected: sheets7-43/44
show known MODE0 enables the held DAC bus and forces decode. Main PIOB7 now gates
host playback0 only when configured as a known low output. Sampling or unknown
MODE stays muted. This correction alone does not fix the silent real-bank PCM.
Next inspect live DAC code/masks, sample requests/addresses and downstream host
muting while the verified firmware envelope is nonzero. No RAM/DAC injection or
firmware note-allocation bypass was added to manufacture sound.

Win64 O3/LTO Release, EII_DIAGNOSTICS=0, cross-build passes with WinMM linked.
Native Windows input/playback/persistence and CPU measurements were not run here.
User screenshot0.37 confirms bank boot and about92%(reported93%) speed with a busy
logical CPU; CPU performance remains an independent unresolved issue. LED graphics
remain pending. This checkpoint stays on the private experimental branch.
