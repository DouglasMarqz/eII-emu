# 0.60: sequence Play investigation and Windows pacing wait

The owner reports that Play raised CPU usage in0.58 as well as0.59. This is
not attributed to the0.59 solver-recovery change. The owner now accepts the
filter sound; the0.60 patch does not modify any filter, solver or audio equations.

## Source selection

Reviewed current open PRs, branch tips, main tip and runtime branch history.
At inspection, fix/front-panel-repaint still points to the verified0.59 commit
f06230c2b9ddf979fdd1de42a1dd71ca1d5b2b91. There is no new separate GLM patch
in those branch/PR heads. No unreviewed patch is imported and main is not merged.

## Supplied sequence disk trial

The unmodified owner-supplied HFE boots to a preset. Pressing the actual scanner
Play contact starts the on-disk sequence. Six isolated native child runs begin
from the same booted firmware state: audio off/Legacy/nonlinear96k, each idle
or Play. No Windows GUI or WASAPI is present in that experiment.

For4M executed CPU clocks (1second simulated), the audio-off owner CPU cost
was about0.88s idle and0.94..0.99s playing. The nonlinear worker rose from
about14..17% to78..94% of one core at96k. These are container measurements,
not the owner's CPU or Windows results. The source owner and DSP worker are
separate threads; their CPU loads must not be confused.

A second trial recording solver counters shows zero fallbacks and zero held
states in both idle and Play at the default profile. Active notes increase
Newton iterations; this trial does not implicate failed-step recovery. The
unpaced experiment sometimes drops DSP blocks, so audible correctness is not
inferred from counters. Short measurement windows do not establish sustained
sequence stability or explain every part of the owner's reported increase.
Private disk data, firmware-derived logs and lab executables are not distributed.

## Proven Windows wait issue

0.59 statically links LLVM-MinGW/libc++'s Windows timed condition-variable wait.
The upstream implementation uses duration_cast<milliseconds>, rounding down.
The0.59EXE disassembly confirms that a remaining interval below1000microseconds
is passed to SleepConditionVariableSRW as0milliseconds. The standard predicate
wait retries until its clock deadline, polling for the fractional tail.

This can consume CPU while emulation is already ahead of wall time. It is
consistent with the earlier owner capture's4.27ms of work per5ms simulated
quantum but approximately1.0owner-thread CPU occupancy. It does not prove that
the entire Play-related rise is caused by waiting.

Primary implementation reference:
https://github.com/llvm/llvm-project/blob/main/libcxx/src/support/win32/thread_win32.cpp
Actual linked0.59symbol:std::__1::__libcpp_condvar_timedwait at0x14005a980;
its below1000microsecond branch and SleepConditionVariableSRW call were checked.

## Change and limits

The machine owner now uses a native Windows auto-reset event for command wakeup
and finite pacing waits. Positive remaining durations round upward to at least
1ms, never to a zero-time poll. Notifications persist across the unlock/wait
boundary; the command predicate stays protected by the existing mutex. Infinite
paused waits, immediate command wakeup, shutdown and owner-thread isolation are
retained. Other platforms retain their standard condition-variable backend.

No CPU/peripheral tick is skipped, no speed ratio is changed, and no audio
buffer or accepted filter model is changed. A Windows sleep may overshoot a
sub-millisecond deadline by up to timer/scheduler latency; normal pacing catches
up by executing the existing quanta. The actual target-machine reduction and
audio jitter remain to be measured. It cannot remove genuine digital/DSP work.

Manual state capture adds HOST WAIT (native-event,calls,timeouts,last-ms) to
allow target verification with HOST ENGINE and the audio/DSP counters.

## Checks

- Positive/expired/ceil-boundary/maximum finite timeout policy:PASS.
- Notification before/during wait, fractional timeout, protected predicate:PASS
  using the native Linux backend. Windows API branch cross-compiles cleanly.
- Front-panel session:owner isolation,matrix/ADC,pause/resume,exact stepping,
  failed-media rollback,command guards,clean shutdown:PASS on Linux.
- Win64O3/LTO,diagnostics off,-Wall -Wextra -Werror:cross-build PASS,zero warnings.
- Windows runtime:NOT RUN here. No claimed target CPU percentage improvement.

Source-only wait tests require no ROM or media:

```sh
g++ -std=c++20 -O2 -pthread -Isrc tests/host_command_wake_tests.cpp -o /tmp/eii-wake
/tmp/eii-wake
```
