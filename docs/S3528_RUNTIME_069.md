# S3528 audible reconstruction stage, 0.69

The nonlinear96/192k renderers can now run the nominal S3528 reconstruction
filter between the SSM2045 four-pole output and the existing C93 coupling/VCA.
Settings contains `S3528 reconstruction (SSM modes)`. It defaults to bypass;
Windows preferences remember the selection. Legacy remains the original path.
The vector panel shows `S3528 ON` when the stage is actually active.

Each voice uses the six-bit active code written by the real firmware at
ports40h..47h, sampled into an immutable48k packet alongside DAC and CV data.
Processing occurs on the existing SSM worker, not a new thread or5MHz loop.
Changing the checkbox does not reset the firmware or recreate the DSP bank.
An A/B change can have a transient as a previously bypassed filter starts
from nominal zero state. Partial/unknown control or input mutes this stage;
no default cutoff is invented. Known control reacquires nominal zero state.

The packet stream and performance JSONL expose `s3528`, `s3528_codes` and
`s3528_masks`; the manual capture includes `HOST CHIP ... S3528=0/1`.
Requested enable persists across Legacy/nonlinear switching; Legacy reports
inactive. Explicit boot takes the current saved checkbox selection.

## Realization and limits

The existing AMI1993 fitted seven-pole/two-notch prototype is inverse bilinear
mapped and realized as four linear TPT state-variable sections, then mapped
to96 or192k with cutoff prewarping. Integrator states survive code changes;
coefficients are cached until that voice's code changes. Nominal cutoff is
`5MHz/(40*N)`, using the established divider table and board clock assumption.
This is a rate-adapted behavioral transfer, not recovered silicon topology.

See [curve provenance](S3528_CURVE_FIT.md) and
[verified source/wiring inventory](S3528_RUNTIME_PLAN.md).
The source-clock laboratory model still includes a pair-average/hold
observation model. This runtime does not simulate that SC sampling, divider
phase, clock images/feedthrough, noise, saturation or external amplifier
calibration. Unity DC gain is deliberate; no undocumented amplifier gain is
introduced. The high-cutoff responses near host Nyquist are warped; the
published3.2kHz curve checks do not establish an error bound over all64 codes.
The separate3202/TL084 switched deglitch dynamics remain omitted at runtime.
The accepted SSM2045 nonlinear solver and its calibration are unchanged.

## Reproducible validation

Native release configuration: GCC13.3, C++20, O3, NDEBUG, diagnostics0.

* `s3528_runtime_tests`: all64 codes settle to DC unity within0.000395 at
  both rates. Rapid per-sample code changes preserve settled DC within9e-15.
  Unknown input/control guards and known recovery pass.
* At code35 (fc3205.128Hz), relative1.06fc loss is-3.95648dB at96k and
  -3.9077dB at192k;2fc loss is-51.2489/-51.3313dB. Maximum delay error
  against seven held-out AMI plot points is15.3205/20.0736us. The40us
  runtime test budget includes this different observation model; it is not
  an AMI tolerance or proof of physical calibration.
* `s3528_voice_tests`:96/192k bypass outputs compare bit-for-bit with a
  frozen0.68 voice, including changing DAC/CV and unknown-input recovery.
  Solver sample/iteration/fallback counters match; enabled broadband output
  differs and stays finite. Unknown reconstruction code mutes output.
* Session/JSONL tests execute an actual Z80 OUT40h, verify the latched known
  code in telemetry, and verify enabling preserves CPU time and DSP generation.
  Requested enable survives model switching; Legacy stays inactive.
* Existing runtime transport, chip audio, vector geometry and AMI curve tests
  pass. These do not prove Windows device scheduling or GUI interaction.

The Windows executable is cross-compiled with LLVM-MinGW20261006 UCRT x86_64,
O3/LTO, static runtime, diagnostics0, Wall/Wextra/Werror. Windows execution
and listening remain user validation; no native Windows test is claimed.

## Real firmware/bank replay

`tools/s3528_runtime_probe.cpp ROM-folder bank.hfe calibration.txt` boots the
user-supplied Marcato bank with the production controller. At183M CPU ticks,
520552 DMA transfers it reaches P01; real Play reaches S01 La Primavera90BPM.
It captures eight seconds at48k boundaries, including eight known S3528 codes.
384000 input packets hash to `5e8282446464fdaf` (probe hash includes codes).
Same tape is replayed OFF/ON at both rates. Production invalid=0; all four
passes have finite nonzero musical output, zero solver fallbacks/held samples.
The upstream solver counters agree for corresponding OFF/ON seconds.

Active seconds4..8 average CPU seconds per audio second on this Linux host:

| Renderer | Bypass | S3528 ON | Increment |
| --- | ---: | ---: | ---: |
| 96k | 0.671851 | 0.689717 | 0.017866 |
| 192k | 1.217900 | 1.308807 | 0.090907 |

This is one offline sequential replay, not a statistically controlled speedup
claim or a Windows real-time guarantee. In particular192k exceeds one core on
this host in both cases. Mean musical RMS OFF/ON is0.107967/0.108568 at96k and
0.107979/0.108620 at192k; the reconstruction stage has not muted this bank.
A full channel-deglitch/silicon-clock/noise calibration remains future work.
