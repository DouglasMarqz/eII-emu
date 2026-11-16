# 0.65 panel polish and target feedback

The owner now reports that Play causes only a small, normal-looking CPU increase
with eight voices, and no longer reproduces the earlier large Play increase.
The supplied screenshot shows version 0.64, a running sequence and 100% machine
speed. Record this as owner-observed improvement, not proof of which earlier
patch caused it, and not a claim that all performance work is complete.

The two supplied idle captures span 32.9836 seconds. Same-thread OS CPU deltas
report about 0.8120 cores for EII machine, 0.1194 for EII SSM2045 and 0.0189 for
EII UI. Other recorded threads have negligible reported CPU time; one new
unlabelled thread has no prior delta and zero reported lifetime CPU time.
The process's thread inventory changes from eight to nine. That does not show
an expensive extra emulator worker. These are fractions of one core, not
whole-PC percentages. The engine's separate latest one-second sample is about
0.857 cores and need not equal the 33-second mean. Current audio missing/drop
rates, MIDI overruns and solver fallback/held counters are zero in these idle
samples. Historical audio-missing totals are unchanged across the two captures.
The remaining sustained CPU target is the machine thread.

## Presentation changes

The supplied Windows screenshot was visually checked against the current panel.
The old font-rendered title visibly lost the second I; it was a clipping/layout
problem. Both Emulator II and Emu Systems, Inc. are now vector outlines extracted
from the owner's EPS, rather than substituted text. The binary DOS EPS wrapper's
PostScript offset/length was honored; embedded preview bytes were not executed.
Ghostscript EPS crop and Cairo SVG conversion preserved the original curves.
The source EPS is not copied into the repository or package. Its SHA256 and
conversion provenance accompany the generated vector command arrays.

The LCD opening widens from 202 to 245 logical units at the same height. The
horizontal dot pitch follows its width; the two live rows and original glyph
bitmaps are retained. The host-volume knob moves left to make room. Numeric
switches are slightly smaller with labels fitting the gaps. Top-group switches
and their LED positions are separated from legend text and module borders.
Dense legends use explicit lines without automatic word wrapping, start below
the top header rules, and have smaller top-column type. The company logo fits
above the utility buttons. Static logo paths remain in the resize-only cache.

No firmware, CPU/controller, solver, filter, audio/MIDI transport or pacing
changes. This release is panel work, not another claimed speed fix. Secondary
low-pass reconstruction filtering and the sampling/analog input path remain
future engineering tasks; they are not implemented or calibrated by this UI.

## Checks

Portable panel tests (30 contacts, keypad/No/Yes, viewport hit tests, six ADC
ranges and host default PCM/gain): PASS. Source SVG rendered and visually
inspected; it is a design preview, not a Windows runtime capture. Win64 strict
O3/LTO diagnostics-off cross-build and executable hashes are recorded in the
package. Actual Windows GDI+ fonts/layout and interaction still need target
verification. The owner's screenshots and captures are not redistributed.
