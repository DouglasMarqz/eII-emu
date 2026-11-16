# 0.64: target thread activity capture

The owner reports no visible performance gain from 0.63 and more activity
across CPU threads. Treat that as the target-machine result. Earlier native
5-7% averages were variable, short container trials, not Windows validation.
The 0.63 source diff did not add any thread creation, worker pool, async task,
UI timer, DSP instance or audio device thread. Increased activity is not yet
attributed to a specific thread, scheduling migration, GUI or driver.

This build adds manual-only Windows thread accounting to the existing State
Capture button. It does not change controller/filter/solver/pacing behavior or
introduce a background sampler. Existing application threads are named once
when starting: EII UI, EII machine, EII SSM2045 and EII WASAPI. Runtime/driver
threads remain separately visible, including unlabelled ones.

The UI enumerates only the current process's threads with Toolhelp and reads
GetThreadTimes. Each live record includes its OS thread ID, description,
cumulative CPU seconds and a one-core busy fraction between manual captures.
Creation time is checked as well as ID, so reused IDs are not mistaken for the
same thread. Exited/new/unreadable threads do not silently receive a zero load.
The first sample has busy=-1 because no preceding interval exists. The total
thread count is a momentary OS inventory; enumeration and CPU reads are not an
atomic process-wide snapshot. Delta loads refer to the interval between clicks,
not the engine's one-second speed window. Unreadable threads are counted.

## Use

1. Boot the same bank and select the same chip mode/calibration as before.
2. Capture once to establish the baseline.
3. Remain idle for about 10 seconds; capture again.
4. Start Play, capture a baseline, then capture again after about 10 seconds.

For the comparison, HOST THREADS / HOST THREAD lines plus HOST ENGINE,
HOST PACING, HOST AUDIO and HOST CHIP are sufficient. A new process starts a
new baseline. The capture and displayed report are still manual observations,
not a continuous trace. Naming and enumeration create no new threads.

## Validation and limits

Win64 O3/LTO diagnostics-off, -Wall -Wextra -Werror cross-build passes without
warnings. The native owner-session regression suite is recorded in the package.
Windows enumeration, naming and CPU numbers cannot be executed in this runner.
No speed improvement or resolution of Play CPU load is claimed for 0.64.
