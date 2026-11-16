# 0.67: signal-flow audit and serial display correction

Baseline: private branch `fix/front-panel-repaint`, commit
`9c7bd8749f0b6b77350550665bd1eacf6ac9b325` (0.66).
This audit follows producers, consumers, clocks, queue limits and ownership.
It is source inspection plus synthetic execution, not a Windows CPU measurement.

## Confirmed defect category: history replayed as live GUI work

The path was:

1. Main Z80 OUT to 2651 register 4 appends to `Machine::serialTx`.
2. Every ordinary worker publication copied that entire history into `report`,
   including DIAGNOSTICS=0 release builds, then escaped every raw byte.
3. Every UI timer refresh converted it again to wide/CRLF text. Any appended
   byte caused a full multiline EDIT `SetWindowTextW` replacement.
4. This continued with the settings window hidden behind the vector panel.
5. State Capture changed the source to a frozen manual report. Consequently,
   taking a capture could stop the repeated growing EDIT replacements.

This is history-length-dependent repeated work, not an infinite firmware loop.
The existing raw UART history is capped at 65,536 bytes, so its escaped payload
can approach 262 KiB. A typical bank may generate much less: the stress result
below is not an estimate of a particular user's session or Play CPU percentage.
The EDIT reflow/presentation cost was not measured on Windows here.

## Reproduction and correction

An asset-free Z80 ROM loops `LD A,90h; OUT (4),A; JP 2`; scanner ROM halts.
The same session/CPU/controller path runs on each side. Lower synthetic PROM
allows the CPU register staging aperture to avoid a deliberately invalid bus.

| CPU clocks | 0.66 live payload bytes | 0.67 default payload bytes |
| ---: | ---: | ---: |
| 200,000 | 38,096 | 69 |
| 400,000 | 76,188 | 69 |
| 600,000 | 114,284 | 69 |
| 800,000 | 152,380 | 69 |
| 1,000,000 | 190,476 | 69 |

At 1,000,000 clocks the full escaped manual capture was **196,945 bytes on
both sides**. This demonstrates preservation of this fixture's report, not a
new truncation or deletion of MIDI history. Native measurements used DIAG=0,
GCC C++20/O2, and identical core objects. Before/after lab output is recorded
in `validation/report-flow-067.txt`.

Normal release snapshots now carry a constant quiet message. Settings has a
persisted **Live serial** opt-in: it displays at most the latest 512 raw bytes
(roughly 2.1 KiB escaped) plus the captured-byte count. Unchanged serial history
reuses the display text/revision. The append-only cache resets on successful
boot and explicit live-view selection. Reaching the existing raw-history cap
retains its existing capture semantics; this is not a new MIDI OUT driver.

Manual capture escapes the full report once and remains frozen. Checking Live
serial explicitly resumes the bounded view after capture; unchecking it while
a manual report is displayed retains that report. Full diagnostic builds still
produce their explicitly enabled diagnostic reports. Hidden/minimized settings
skip report/status formatting and old LCD/LED invalidation; reopening refreshes
them. Vector-panel updates and asynchronous MIDI export continue normally.

## Other followed paths and limits

| Path | Observed design | Audit conclusion |
| --- | --- | --- |
| UI/MIDI -> session -> firmware | Commands are transferred under the session mutex; emulation applies them as owner. MidiWire has a 4,096-byte limit and 1,280 CPU clocks per MIDI byte. | No demonstrated feedback loop here. Burst rejection/overrun remains separately observable. |
| CPU -> DRAM/WAIT -> controller | 4 MHz CPU and controller half-edge progression retain ordered service boundaries. Production spans stop at observable transitions. | Do not replace physical WAIT/interrupt order with guessed instruction skips. 0.63 span gains were modest and not established on the user's Windows machine. |
| Playback latches/CVs -> DSP | Immutable 48 kHz pin snapshots; 96/192 kHz eight-voice nonlinear rendering. 96-frame blocks, eight queued blocks; oldest input can be dropped on overload. | Producer does not wait for DSP completion. DSP lock is released before rendering and before calling output sink. No circular wait found in this inspected path. |
| PCM -> WASAPI | Bounded queue, short locked copies, event-driven output; callback does not run emulation or chip solver. | A bounded queue can still underrun. A zero interval counter is evidence for that interval, not proof of all-session health. |
| Owner pacing -> commands | Native event with upward rounding of positive waits; mutex released during native wait and predicate rechecked. | Preserve the verified 0.60 no-zero-poll correction; no new spin fix is claimed. |
| Filter adapter -> chip | `fcTarget`/board resistance and VCA current setters are visited each oversampled voice sample. | Candidate repeated invariant work, not a demonstrated defect. Leave accepted sound/solver unchanged until measured and equivalence-tested. |
| Snapshot -> UI | Full manually captured strings are still copied into snapshots. | Bounded remaining cost; escape/reflow is now revision-gated. Could use immutable shared report storage if profiling justifies it. |

The confirmed defect is in host presentation. This patch changes no emulated
CPU, PROM, PIO, DMA, timer, memory, MIDI wire timing, audio queue, pacing, chip
solver or filter transfer. It does not implement the secondary S3528 stage,
recording/SAR transport, MIDI OUT or ASIO.

## Validation and next discriminating check

`panel_report_text_tests`: default constant text/revision despite history
growth; bounded explicit view; unchanged cache; binary escaping; untruncated
manual capture; reset with same-size new contents.

`live_report_session_tests`: actual synthetic firmware TX flood through the
session, opt-in without advancing the firmware, full frozen capture, resume
live view and reboot. Existing panel session tests explicitly opt in only where
they inspect serial bytes. See `validation/report-flow-067.txt` for executed
checks and build provenance.

The Windows user test is idle -> Play -> Stop, initially with Live serial OFF,
then ON, with paired manual captures only when needed. The first capture can
still deliberately replace a live view with frozen text. A separate source
and target measurement is required before calling this the whole Play fix.
The audit has found and corrected one category; it is not a proof that no
other bottlenecks or timing defects exist.
