# 0.66: renewed Play report and sparse GUI paint work

The owner reproduced the Play CPU symptom again and supplied a new manual
capture. The earlier report that it was no longer reproducible is therefore
not treated as closure. Keep the accepted sound path and panel geometry.

## What this capture establishes

The engine reports valid production state, about 0.871 cores in its latest
one-second sample, 4.52 ms mean quantum for 5 ms simulated and +1.487 ms pacing
lateness. No rebases or discarded debt. WASAPI is running without timeout or
restart; current audio missing/drop rates are zero. MIDI UART overruns are zero.
The nonlinear96k worker load is about 0.468, with no drops, failures, fallbacks
or held solver states. This does not show the old zero-time wait spin or a
solver recovery storm at the captured instant.

Thread inventory is nine with all readable. This is the first manual thread
sample in that process: every busy value is -1, so there is no current per-thread
interval comparison. UI lifetime CPU is about294s, machine351s, DSP16.75s. These
are cumulative since thread creation; process age and earlier UI actions are
not established by the capture. They warrant inspecting UI rendering but do not
prove the UI is currently saturated or caused the reported Play symptom.
Historical missing/clipped totals also do not locate a current glitch.

## Source-level rendering issue and patch

A Windows update region can contain several separate dirty rectangles. The
previous renderer replaced it with PAINTSTRUCT::rcPaint, its single bounding
rectangle. GDI+ visibility tests then included clean gaps between changed LEDs,
controls and status. In that situation an unchanged LCD inside the bounding
rectangle could pass IsVisible and issue all1280 dot drawing calls.

WindowsPaintClip now saves GetUpdateRgn before BeginPaint validates the region,
then applies that exact region to GDI+. If the region API fails, the previous
bounding-rectangle fallback remains. Static-cache copying and dynamic drawing
retain the window's sparse invalidation shape. Volume, LCD and footer rendering
are skipped when outside it; key/fader checks already existed.

When the LCD really needs drawing, non-overlapping lit and unlit rectangles are
submitted in two FillRectangles batches, with two brushes. The prior code
constructed1280 brushes and issued1280 separate GDI+ rectangle calls. Glyphs,
positions, colors, two-row text, firmware timing and snapshot cadence remain
the same. The window title version is also corrected.

This removes concrete redundant UI work. It is not yet evidence that the full
Play symptom is fixed on Windows. There is no new thread, timer, background
monitor, audio buffer change, engine scheduling or solver/filter modification.
The target question remains whether repeated manual captures during the
symptom show high EII UI CPU, EII machine CPU, or another thread.

## Validation

Win64 O3/LTO diagnostics-off, -Wall -Wextra -Werror build: PASS, no warnings.
Source inspected against the SDK's HRGN SetClip overload. Windows update-region,
GDI+ batch-render appearance and target CPU savings are not runtime-tested here.
No unrelated visual geometry or host timing change is included. The owner's
capture and its firmware/media-derived content are not redistributed.
