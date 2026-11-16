# 0.56: stiff filter solver and host audio scheduling

The nonlinear filter could enter persistent expensive fallback at accepted
service settings. A separate synthetic full-scale alternating DAC test, with
small OTA capacitors and high transconductance, reproduced the fault without
ROMs, disk images, MIDI or the emulator CPU. The old solver used an explicit
Euler predictor and, on failed Newton convergence, another explicit Euler
step. That fallback is unstable for stiff states and can keep injecting energy
after the excitation stops. Finite values alone do not establish stability.

The normal implicit trapezoidal equations, Jacobian, tolerance and iteration
budget remain unchanged. When T*gm exceeds twice the smaller first/third
integrator capacitor, the initial Newton estimate uses the previous state
rather than an explicit step. Failed normal solves retry trapezoidal from the
previous state with bounded extra line search, then backward Euler with the
same instantaneous feedback equations. If both fail, the previous finite state
is held for that sample. No explicit fallback, unbounded retry loop, firmware
skip, arbitrary Q clamp or model reset is used. Recovery is a numerical safety
path; backward Euler damps differently from trapezoidal and a held sample is
not a claim of accurate analog behavior at every extreme service setting.

Solver diagnostics distinguish fallback entries, successful implicit recoveries
and held samples. A synthetic regression covers 96/192kHz full-scale excitation,
quiet tails, subsequent notes and a live high-Q configuration transition.
It fails against the previous solver. A frozen 0.54 oracle independently checks
ordinary operation. The service calibration schema, registry name, saved values
and Legacy filter path are unchanged. S3528 remains bypassed.

Host pacing also previously retained unlimited wall-clock debt after a stall.
It could produce a sustained catch-up burst into bounded audio FIFOs. The host
now rebases overdue deadlines beyond 20ms; every emulated CPU tick is still
executed. This discards wall-time debt, not CPU work. Actual ticks/wall-second
still determine the speed indicator; the rebase count and discarded wall debt
are reported separately. This cannot make a persistently overloaded machine
run in real time. It only prevents replay of obsolete scheduling debt.

The Windows WASAPI and DSP workers request scoped, best-effort Audio MMCSS
scheduling. The WASAPI worker also requests a scoped 1ms timer period; matching
revert/end calls run on teardown. Success is reported, not assumed. The CPU
worker stays at its normal scheduling priority. DLL APIs are loaded dynamically;
there is no new redistributable, affinity lock, busy spin or global process
priority change. Target Windows runtime behavior remains to be tested.

The service window reports recent host dropped/missing frames per second,
not just lifetime counters containing boot, pauses and earlier model switches.
DSP queue wait and its maximum are also available in manual state capture.
Cumulative counters remain available. Calibration drags no longer force a full
snapshot for every command; the normal UI publication cadence remains.

## Reproducible asset-free validation

Use CMake's `filter_stiff_tests`, `filter_solver_tests`, `filter_service_tests`,
`host_pacing_tests`, `front_panel_session_tests`, `chip_audio_tests`,
`runtime_audio_tests` and `ssm2045_chip_tests`. Direct native compiler commands
and results for this release are in `validation/audio-stability-056.txt`.
No private firmware, bank sample or disk content is needed by these tests.

Microsoft API references:
- https://learn.microsoft.com/en-us/windows/win32/api/avrt/nf-avrt-avsetmmthreadcharacteristicsw
- https://learn.microsoft.com/en-us/windows/win32/api/avrt/nf-avrt-avrevertmmthreadcharacteristics
- https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod
