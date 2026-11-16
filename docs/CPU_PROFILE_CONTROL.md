# Machine-thread CPU profile (control audit 2026-10-31)

Linux g++ -O2 -pg build of `slider_latency_probe` with the real ROMs, Grand Piano
bank, production controller and no DSP. The run covers the 46 s boot plus
40 s of steady play, with idle and sweep conditions for sliders A-D and both
wheels.

Steady state costs about 0.9 s of host time per emulated second on one core
(-O2, no -pg; core probe and the VST engine agree, matching the VST
`machine_load` 0.8-0.9). Correction 2026-10-31: an earlier figure of
0.24 s came from a probe loop labelled "4000 ms" that actually ran 1 s.
The same mislabel applies to the "per 40 emulated s" wall figures in the
perf commits (they are per 10 emulated s); the relative gains stand.
Consequence: the machine runs only ~1.1x realtime, so a fast-forward
("turbo") disk load on the firmware path cannot be much faster than real
time.
Idle and slider/wheel sweeps differ by less than 10%, so moving the panel
controls does not raise the machine cost.

| Share | Block | Main functions (self %) |
| --- | --- | --- |
| 60.4% | Voice controller: 10 MHz PROM sequencer, channel timers, DRAM port | ControllerTiming::risingClockImpl 20.5, ChannelTimers::advanceRuntimeHalfClocks 6.8, settleServiceBus 6.6, production::UpperPath::rising 6.5, driveTimingSelection 4.7, fallingClock 3.0, DramPort::settle 2.6, advanceQuietClocks 2.7 |
| 21.6% | Z80 cores, CTC, Machine::step | Machine::step 9.8, z80_tick 4.3, CtcEventRuntime::tick 2.5 |
| 11.6% | Floppy/disk path, run on every CPU tick even with no disk activity | updateFloppy 4.7, DiskReadPath::advanceCpuBoundary 3.7, separator 1.0, DiskDma 1.0 |
| 3.8% | Scanner<->main PIO link tick | Machine::pioLinkTick |
| 1.1% | Scanner panel/ADC0809 | ScannerControls::driveBus 0.9, Adc0809::clock 0.25 |

Correction: the ADC0809 SAR loop is not a hotspot. It costs 0.25%.

## Plan, by gain per risk

1. Floppy/disk quiescent fast path, up to ~11%. Skip the per-tick drive,
   separator and SIO-receiver work while no drive is spinning or selected,
   the receiver is idle and the PIO B output is unchanged. Advance it in one
   step on the next access. Exactness is gated by the boot/disk
   fingerprints and the suite.
2. PIO link: run it on events instead of per tick, ~4%.
3. Voice controller, ~60%: extend quiet-clock batching to the
   channel-timer and service-bus paths. This is the transaction-engine work,
   checked by A/B fingerprints against the reference controller.
