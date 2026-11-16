# 0.41: certified controller spans, performance goal still open

0.40 remains the preserved first-sound reference. This checkpoint reduces
redundant controller work; it does **not** establish low Windows CPU usage or
complete the general event-driven engine.

## Execution change

The ordinary runtime can advance an idle voice tail (phases10..15) as an
interval instead of calling the full rising/falling controller on every
source edge. The loaded upper PROM is checked for the required zero-output
transitions at both possible read-input levels. A lower transaction must be
certified idle, control pins must be definite, the channel request must be
low, and the CPU request pipeline must be inactive/completed. The DRAM port
must prove that no strobe or latch-clock edge can occur.

The interval ends at a CPU boundary, timer/request deadline, or voice-slot
boundary. IC69 still receives the current CPU address. The elapsed phases,
shift register, pending CPU write handshake, virtual lower pipeline/ALU state,
logical clock counters and slot exit are advanced to their exact new state.
There is no future sample read, sector injection, WAIT shortening or assumed
unknown-data value. Actual CPU/sample DRAM and DAC events, the ninth slot,
uncertain inputs and unsupported PROMs use the existing edge implementation.

The runtime also publishes aggregate controller observations once per CPU
interval. The channel and ACK decoder consume live internal pins throughout
the interval. CPU-visible observations are current when the interval ends.
Source-clock analog observers bypass both changes and still see every edge.
`ChannelTimers::enableControllerSpans(false)` selects the previous edge/publication
route for comparisons. This is not a change to the emulated clock frequency.

## Measurement

One pinned native GCC13 O3/LTO/NDEBUG, diagnostics-off process, after the
original disk/bank boot. A = previous source-edge route; B = spans plus batched
publication. ABBA order in each stage, two samples per mode, each8M CPU clocks
(two simulated seconds), actual basic audio enabled:

| Stage | A CPU seconds, mean | B CPU seconds, mean | CPU-time reduction |
| --- | ---: | ---: | ---: |
| Idle | 2.919615 | 2.689149 | 7.89% |
| MIDI note held | 2.925300 | 2.692568 | 7.96% |

Each B interval collapsed about8.555M of20M source clocks. These are skipped
interpreter invocations, not missing hardware time. Logical clocks continue.
The run produced3,192,000 host frames with nonzero note audio and3/3 UART
bytes/firmware reads. Raw samples: `validation/runtime-041-span-benchmark.json`.

Both modes remain slower than real time on this native host (B about0.743x).
This is **not** a Windows Task Manager measurement, a comparison with EIII,
or evidence of reaching0.7% CPU. The larger runtime redesign remains needed.

A separate known-clock DRAM rewrite passed state checks but regressed about
2.71% in a pinned24M-clock ABBA trial. It was removed. See
`validation/runtime-041-rejected-known-clock.json`; no rejected code remains.

## Verification and next boundary

The new regression compares full future controller state, all64 RF words,
DRAM/DAC latches and counters, timer/divider states, public observations and
service pins with the edge oracle over304,000 CPU ticks. It includes dense
idle intervals, interleaved memory/CPU-staging writes, uncertain CPU inputs,
reset, a PROM that forbids skipping, runtime mode changes and source observers.
It runs with synthetic and externally supplied original PROM fixtures.
Existing randomized controller/DRAM tests remain intact. Source-location
failure messages were added to the shared test helper.

Reproduce the measurement with a Release build of `runtime_span_benchmark`,
passing a ROM directory and bank image. Boot is excluded from its ABBA stages.
Full evidence and binary provenance are in `validation/runtime-041-provenance.json`.

The next substantial boundary is timed execution of **active** upper-controller
CPU/sample DRAM transactions, including the ninth-slot staging/background
operations. The retained spans cover an idle subset; the active source-clock
interpreter remains the main architectural limitation. Preserve the0.40
reference, memory/DAC timing and exact audio regression while replacing it.
