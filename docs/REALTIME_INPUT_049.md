# 0.49: production publication, bounded audio, MIDI host tools

User0.48 capture: production invalid0, UART766received/read,0overruns and no
pending wire bytes. Pacing is377.497ms behind; host PCM queue is480frames/10ms,
with accumulated drop/underrun counters. This differs from the earlier code4
CAS failure and does not establish an IRQ deadlock or an audio FIFO backlog at
that instant. User also reports160ms queue buildup in other moments.

## Release work removed at the correct boundary

Production previously materialized its public observation mirror on every CPU
interval (4million/s), even with diagnostics compiled out. Ordinary host-owned
Release Machine execution now publishes that mirror at Machine::run boundaries
and external PCM callbacks. CPU WAIT is still updated at every CPU boundary;
channel/ACK, DRAM data, DAC, register and ALU consumers use live production
getters. Invalid-state stop materializes the failure before throwing. Standalone
controller stepping, diagnostic builds and the reference engine retain their
existing observation cadence. This is no skipped hardware clock or speed-meter
rescaling. An explicit switch allows the per-CPU production route as an oracle.

An asset-free1M-tick comparison checks exact CPU pins, memory, boundary reports,
PCM and invalid-state stop, including reference startup and current counters in
PCM callbacks. Original-bank preset01 COW runs start from an identical boot and
compare identical dense input: resulting PCM, main-memory and scanner hashes
match between per-CPU and block publication. Wall timings varied substantially
on this host, so process-CPU timings are retained in the final comparison. No
Windows CPU-utilization or guaranteed headroom percentage follows from them.

## Audio age bound

AudioQueue defaults to960frames/20ms at48kHz, retaining the newest chronological
frames when the producer exceeds the bound. A stalled consumer cannot leave
160ms of old audio in that default queue. WASAPI raises the limit if necessary
to hold one reported endpoint buffer (bounded by existing8192-frame storage),
so a large device buffer is not forced into permanent underruns. Capture state
shows limit/highwater as well as drop/underrun counters. Highwater resets on
queue clear/device reset. The endpoint buffer is separate from the host FIFO.

This bounds queued audio age; it does not speed up an emulated CPU that runs
below real time, eliminate device latency, resample device clock drift, reset
pacing debt or conceal underruns. No firmware clocks are dropped to catch up.

## MIDI controls, optional adapters and capture

The original firmware has separate physical wheels and MIDI A/B/C sources.
Owners Manual, General Instructions6 and Preset Definition30/MIDI supplement,
describes routing incoming controllers to those sources and destinations. Raw
pitchbend/CC1 is therefore not automatically the same as moving a panel wheel.
The current bank's exact MIDI A/B/C setup has not been verified by the native
menu experiment, so no claim that it is configured incorrectly is made.

New UI options (both OFF by default and persisted):

* **MIDI to panel wheels**: incoming E0..EF14bit pitchbend drives the scanner's
  physical left-wheel ADC pin (8192=2.5V); CC1 drives right wheel0..5V. Those
  controls then follow the original firmware's physical-wheel path. The GUI
  mirrors the MIDI-induced positions without fighting a captured trackbar drag.
  All input channels drive these global wheels. Original MIDI bytes also reach
  the UART: use this bridge with presets assigned to physical wheels; a preset
  already routed through MIDI sources can otherwise respond by both routes.
* **Note Off as90/0**: explicitly convert incoming8n note-off to9n velocity0,
  preserving channel/key and dropping release velocity. This is a host
  compatibility adapter, not a receiver/model change. Raw default stays exact.

**Capture MIDI** is OFF on launch and is not persisted. When enabled, a bounded
2048-event ring records host arrival/apply monotonic timestamps, simulator tick,
original bytes, adapted wire bytes and wire-enqueue acceptance. Save trace writes
TSV asynchronously, with no per-snapshot bulk trace copying. Capture is not a
complete machine-state/input replay. Packet data bytes are validated as7bit;
SysEx remains outside this short-message input path. Nothing is uploaded.

The native worker test verifies the adapter drives actual scanner analog pins,
raw/wire trace data and UI revision; standalone tests cover limits, disabled
capture, wraparound, malformed bytes and ASan/UBSan execution. Normal disabled
capture adds no per-CPU-clock recorder work.

## What the dense-input tests show and do not show

At100note-on/off pairs per simulated second, the supplied preset01 can produce
UART overruns with explicit80 note-offs in the current model. A90/0 sequence at
the same rate is fully consumed and its ACV decays. At20pairs/s, both forms are
consumed with no overrun and the bank has a multi-second audible release tail.
The100pairs/s overrun case does NOT reproduce the user's zero-overrun capture;
it is not evidence that the reported freeze has been fixed or that this is a
real EII firmware limitation. SCN2651 buffering/interrupt timing remains a
possible fidelity issue; do not invent a FIFO or hide overruns without proof.
Primary device reference: Philips SCN2651 product specification,1994-04-27,
SR1/SR4 descriptions pp9-10 (https://pdf.dzsc.com/SCN/SCN2651.pdf).

For a Windows recurrence, enable Capture MIDI before playing and save the TSV
and Capture state while the symptom is present. This gives the actual note-off
forms, pedal/controller events and applied timing that the snapshot lacks.

## Filter status: explicitly unfinished

This checkpoint does not change the analog DSP. BasicPlaybackVoice still uses
the zero-Q SSM2045 transfer and bypasses S3528. Its existing two sections already
use trapezoidal/TPT integrators with algebraically solved local feedback. The
complete SSM2045 resonance loop, QCV-to-current network and S3528 production
reconstruction integration remain pending; calling the current voice a complete
ZDF SSM2045 would be incorrect.

Visually reviewed manufacturer block diagram/response equations and board7-45:
QCV feeds pin17 through22k plus the internal1.8k input; the manufacturer gives
120..185uA (typ150uA) oscillation current for its example circuit. The EII uses
different capacitor values. Derive the coupled Q feedback from that circuit,
solve it without an artificial sample delay, preserve states during parameter
changes, and validate gain/oscillation behavior. Do not substitute an unrelated
generic ladder merely to use the ZDF label. Absolute tuning/nonlinear limiting
still need calibration. The existing S3528 typical amplitude/group-delay fit
should be adapted to host-rate rendering with cached coefficients/state-aware
parameter updates; the20MHz laboratory analog observer is not a suitable normal
Release audio backend. Keep its uncertainty/calibration status explicit.

Fresh Win64 Release cross-compilation is separate from Windows UI/device/audio
validation. No ASIO or calibrated analog fidelity claim is made here.

Final original-bank process-CPU comparison (one paired COW trial, this host):

| Phase | Per-CPU mirror CPU seconds | Block mirror CPU seconds |
| --- | ---: | ---: |
| idle4M ticks |2.08102|1.43055|
| dense6.4M ticks |3.86688|2.56377|
| release4M ticks |1.82490|1.54773|
| later release16M ticks |7.75034|6.35624|

Both outputs: PCM hash333670035218626480, memory8342791576837532059,
scanner2363400136142823169; UART960/960/0. CPU-time data avoids counting
idle host waits, but frequency/cache/load/order still affect a single trial.
Earlier wall-only runs varied widely and are not a guaranteed speedup estimate.
The final crossbuild includes the external PCM-callback observation boundary.

Windows status and manual Capture state now also include WinMM driver error,
more-data/congestion, unsupported-message and sink-rejection counts. Engine UART
overrun0 does not describe losses before bytes reach the emulated UART. A
more-data notification alone is not proof that a packet was lost. Host context
is copied only when Capture state is requested, not as a live trace.
