# 0.57: DC-safe host PCM and observable Windows audio recovery

A modeled VCA output offset is a constant current even when its audio inputs
are zero. Summing eight voices can therefore produce a large digital DC
baseline. Zero-filled host underruns then switch abruptly between that
baseline and zero, making large audible pulses. The old Legacy model has no
adjustable VCA offset; it was not an equivalent test of this case.

The nonlinear bank now AC-couples the summed host signal before its digital
limiter, using a 2Hz first-order bilinear high-pass at48kHz. This is explicitly
host conditioning, not an additional calibrated EII/SSM circuit component.
The per-chip filter, VCA current/offset, board coefficients and saved service
profile are preserved. A DC estimate is exposed as `outputDC` and digital
limiter events as `clippedFrames`. Steady DC no longer consumes host headroom.
The startup/DC-step transient settles; bass has the documented2Hz response.
This removes the large DC-to-zero discontinuity, not missing audio itself.
Legacy DSP and firmware execution remain unchanged.

The Windows output previously waited indefinitely for render events and left
multimedia scheduling flags set after a runtime exception. Those flags were
not proof that rendering continued. Now manual capture includes `running`,
submitted frame count, event count, event timeouts, reopen attempts, time since
last submission, current error and retained last error. Thread exit clears
running/MMCSS/timer status. Submitted frames mean API submission, not proof
that a physical loudspeaker produced sound.

Runtime API errors,500ms event silence or500ms without buffer write progress
close and reopen the selected endpoint stream, at most three retries per
manual start, with a stoppable100ms delay. Each attempt owns and releases its
COM/event/client objects. The emulator and MIDI are not restarted. Failure
remains visible and the existing Refresh outputs button is the explicit
manual recovery path. No retry storm, firmware reset, spin loop or unbounded
wait for a missing render event is introduced. A driver blocked inside an API
call is outside this event watchdog's scope.

Tests: independent DC rejection/DC-step/20Hz440Hz10kHz response, eight offset-only
voices with real zero-filled queue underruns; service profile and worker gap
continuity; native panel/session, Legacy runtime, worker96/192 and frozen
solver oracle; ASan/UBSan. Windows x64 cross-compilation is distinct from
Windows driver/runtime validation, which is still needed on target hardware.

The reported intermittent per-voice nonlinear noise/reuse symptom is not
claimed fixed by this release. No arbitrary filter state reset, Q clamp or
user calibration overwrite is included. Host underrun/overrun origin may need
further timing/device investigation; the new diagnostics distinguish it from
a live DSP solver failure. S3528 remains bypassed.
