# 0.59: bounded implicit filter recovery

## Reproduction

An isolated synthetic input reproduces persistent `held` states in the 0.58
SSM2045 solver. It uses the reported high-Q calibration, fixed QCV=3.125V and
FCV=0.429688/0.488281/0.546875V. The synthetic stress alternates +/-0.8V at the
48k input cadence and then becomes zero; this is not a capture of the user's
sample bank and does not establish the exact signal amplitude in that bank.
The filter alone has no Machine, MIDI, host audio queue, WASAPI or threads.

Over 0.5 seconds per case, the old 96k solver holds 23937/35999/729 states;
the old 192k solver holds 57061/14130/65811 states. These are failed nonlinear
solutions, not intended analog resonance. The new recovery yields zero held
states in all six cases. The public regression uses shorter excitation plus
zero-Q tail/retrigger and fails with the old source, passes with the patch.

## Change

The ordinary trapezoidal equation, predictor, Newton tolerance and iteration
limits are unchanged. Existing whole-step trapezoidal/backward-Euler recovery
also remains first. If both fail, try complete backward-Euler integrations
with 2,4,8,16,32 subintervals, starting every attempt from the original finite
state. Each subinterval must converge. Commit only a whole completed sample;
if every bounded attempt fails, preserve the existing held-state safeguard.
No explicit state injection, arbitrary reset, Q clamp or parameter substitution.

This modifies only numerical recovery in the already failed-step case. It can
introduce backward-Euler damping there; it is not a claim of transistor-level
or physically measured calibration. A deliberately severe input can still be
expensive, and the fixed subdivision budget cannot guarantee convergence for
all editable configurations.

## Checks

- New recovery test: 6 rate/control cases,9130 converged fallback samples,
  zero held; zero-Q tails below1e-9 summed squared output and retrigger responds.
- Old source fails that regression (`failed sample recovery retains old state`).
- Existing 48k-sample frozen ordinary-solver comparison: max error5.64479e-15.
- Existing stiff test:691200samples,quiet tails/retrigger/extreme transition PASS.
- Vendor full VCF/VCA test PASS; recovery ASan/UBSan PASS (leak detection off).
- Native core compiles O3/DIAG0 with GCC warnings-as-errors except the pre-existing misleading-indentation warnings in filter_calibration.h. Worker adapter/queue/partial-flush/teardown tests pass at96/192k.
- Eight-case synthetic unpaced run: old18.427s/new12.147s owner CPU in this
  container. Other compilations overlapped part of measurement. This is only
  a rough stress-cost comparison, not a target-PC or full-emulator speed claim.

The current user's screenshot also shows increasing DSP dropped frames.
This solver patch does not repair worker queue discontinuities or establish
that all audible crackle is eliminated. The separate fast-note firmware hang
is not reproduced or claimed fixed. Host CPU saturation remains a separate
performance issue. Full Windows target-machine testing is pending. The solver patch was first
preserved in source commit197afa4. The LLVM-MinGW20261006 compiler was then
restored from its verified official archive; a 0.59 Win64 O3/LTO release build
with EII_DIAGNOSTICS=0 and warnings-as-errors passed with no warnings.
Cross-compilation is not a Windows runtime or audio-quality test.

Reproduce without private assets:

```sh
g++ -std=c++20 -O3 -Ivendor/ssm2045-chip/include tests/filter_recovery_tests.cpp vendor/ssm2045-chip/src/chip.cpp -o /tmp/eii-recovery
/tmp/eii-recovery
```

## 0.59 build receipt

Freshly restored source:155Git blobs verified before version changes. Native
recovery regression, frozen solver, stiff test and DSP worker tests pass again.
Win64 EXE SHA256:
6736943fb0f34f851e7d70c155719503d1dd9cddfdddd6e4d95849098479a3ca.
PE32+x86-64,system/UCRT imports only. No ROMs or media distributed.
Windows execution and the actual audible crackle remain unverified here.
