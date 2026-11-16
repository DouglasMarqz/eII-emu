# 0.62: LCD proportions and renewed Play investigation

## Owner capture

The owner supplied a manual capture during sequence Play on 0.61. This is a
single observation, not a live trace. The main emulation thread reports 0.952628
CPU occupancy and a 4.55163 ms mean work quantum for 5 ms simulated time.
Wall-minus-simulated is +0.1519 ms with no pacing rebases or discarded debt.
The Windows native-event wait is active with a positive 1 ms last timeout.
This capture does not show recurrence of the old zero-time wait polling.

Current audio interval missing/dropped rates are both zero. Historical missing
and clipped-frame totals are cumulative and do not establish when a glitch
occurred. The nonlinear96k worker reports about 42% load, zero queued frames,
zero drops/failures/fallbacks/held states. MIDI UART overruns are zero. These
observations place the immediate CPU investigation on the digital owner thread;
they do not establish that the full Play problem is resolved or rule out every
intermittent issue.

## Private native measurement

Booted the unmodified supplied sequence image through the real firmware and
SIO/DMA path. Forked the same boot state into idle and actual Play-button runs.
Disabled runtime audio in both to isolate the digital workload. Each measured
20 million CPU clocks (five simulated seconds) after the same warmup.
Collected statistical stack samples only during the measurement windows.
Firmware, media, private lab source and raw samples are not distributed.

With O3, diagnostics disabled and native sampling/frame-pointer overhead, idle
cost roughly 0.88-0.92 CPU seconds per simulated second; Play roughly 0.96-1.05.
These are container measurements, not target Windows percentages. Approximate
sample attribution was:

| Digital subsystem | Idle | Play |
|---|---:|---:|
| Production controller | 40% | 38% |
| Timer/request scheduler | 24% | 22% |
| CPUs and peripheral bus | 25% | 27% |
| Disk transport | 4% | 5% |
| Other | 7% | 8% |

The profile confirms substantial digital work even without any audio solver.
It does not identify a runaway feedback loop. The controller and source-clock
scheduler together account for about 60% of samples in both states. The next
engineering target is their event processing, preserving CPU boundaries, WAIT,
DMA and channel requests against the reference oracle. No speculative timing
shortcut is included in this release, and no Play CPU reduction is claimed.

## LCD correction

The display opening changes from 202x102 to 202x72 logical units. Two rows are
centered with independent horizontal/vertical dot pitch; character dots are
1.35x2.0 logical units instead of the previous tiny square dots. Layout and
SVG preview share the revised opening. The live glyph bitmap and firmware
text remain unchanged. This corrects presentation, not LCD emulation timing.

Only GUI geometry/rendering, version labels, preview and this document change.
The 0.60 native wait, machine core, MIDI, audio queue, accepted filter and solver
remain byte-for-byte unchanged from 0.61. The current service calibration is
preserved; this release does not substitute a new calibration profile.

Validation: portable panel/contact/viewport/gain tests PASS. Source-based SVG
rendered and visually inspected. Win64 cross-build results and executable hash
are recorded in the delivery package. Windows runtime/DPI and target CPU
measurement remain unavailable here.
