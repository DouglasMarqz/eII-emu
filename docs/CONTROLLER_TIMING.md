# Controller clock, scan and sampled inputs (checkpoint 0.9)

Service PDF page 80 / schematic 7-17, page 81 / 7-18 and parts list PDF
144 / printed 9-6 were visually checked. IC111's drawing reads 74S181,
but its counter pinout and the parts list identify **74S161**, as for IC112.
It is not modeled as an ALU. IC110 is LS164; IC140 is S174.

## Circuit equations

Both S161s and LS164 use rising +C10MHz.1 edges. Evaluate all old outputs
before updating any of them.

| Part | Wiring | Result |
| --- | --- | --- |
| IC111 state counter | D..A=1000, enables high, /CLR high, /LD=!RCO | Counts 8..15, then synchronously loads 8 |
| IC112 scan counter | D..A=0000, EP high, ET=IC111 RCO, /LD=!(CHSL3 & RCO), /CLR=CRST | Advances once per eight states; slots 0..8, then loads 0 |
| IC110 shift | A=1, B=IC111 RCO, /CLR high | Shifts OLD carry; Q0=STATE0, Q3=STATE3 |
| IC140 input latch | /CLR=CRST, CLK=NAND(STATE0,!C10MHz.1) | Samples on rising source edge ending the OLD STATE0 interval |

The micro-PROM addresses use IC111 low three bits (UCNT2..0). Its high bit
is not an additional microstate address bit. IC112 CHSL2..0 select the output
request mux; CHSL3 distinguishes the ninth slot. At steady state, STATE0 is
high during low-state 0; STATE3 follows three source clocks later.

IC140 observable outputs are UREQ.1, PRECAR, LOOPMD, DECFLG, BACKMD.
The latter four sample RFD23..20. The unused B input/output is not exposed.
The model packs request at bit 0 and those four flags at bits 1..4, with a
known-bit mask. Sampling occurs before scan/shift updates, not from the next
slot. A low CRST asynchronously clears scan and IC140 inputs, while state
counter and shift continue: their /CLR pins are tied high.

Primary behavioral references (S/LS family function tables, no physical
S-series propagation calibration claimed):
https://www.ti.com/product/SN74LS161A
https://www.ti.com/product/SN74LS164
https://www.ti.com/product/SN74LS174

## Startup and machine connection

CRST is assumed initially asserted as part of Machine's initial CPU reset,
then released before stepping. No reset pulse duration is invented.
IC111 can start in any of 16 states; IC110 is initially unknown. The model
retains one trajectory per possible state-counter power-up value, including
its own shift known-mask, scan count and captured inputs. The shift becomes
known within each trajectory after eight clocks, but the absolute microphase
remains unknown across trajectories. Consensus exposes only equal known
bits. This is a conservative abstraction; correlations with the separately
modeled channel request/ACK uncertainty are not retained.

A specified constructor phase is available for characterized fixtures and
synthetic tests. Machine uses all 16 trajectories; it never silently picks
one power-up microphase. Reasserting reset preserves the running state/shift.

ChannelTimers now runs five 50ns half-edges per 250ns CPU frame. Clock starts
low as a scheduler-origin convention: first frame has three rising and two
falling edges; second has two rising and three falling edges. After two CPU
ticks each has five. The 8254 and output divider retain their falling-edge
ordering; rising edges drive ControllerTiming and the resulting CHSL low
bits feed ChannelServiceBus. Thus counter and divider clocks no longer have
their positive/negative edges lumped into one event. Absolute clock-to-CPU
phase, pin propagation, gated-clock glitches and setup/hold remain idealized.
The complete-source-period helper preserves the current half-cycle phase.

Actual clocked PROM output, MPRQ/MPRD handling, register files, memory flags,
DRAM arbitration and sample service remain unfinished. RFD flags remain
unknown in Machine. UCD08 remains unknown; no invented PROM word supplies
ACK. The existing PromLatch remains a separate lookup/latch kernel.
This milestone establishes timing and sampled inputs, not complete service.

## Validation

```sh
bash tools/build-native.sh > validation/tests-controller-timing.txt
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp tests/controller_timing_tests.cpp -o build/controller_timing_tests_san
ASAN_OPTIONS=detect_leaks=0 build/controller_timing_tests_san > validation/sanitizer-controller-timing.txt
build/eii_probe ../roms/main.bin ../roms/scanner.bin 40000000 > validation/controller-main21-scan31.txt
build/eii_probe ../roms/diag.bin ../roms/scanner.bin 4000000 /tmp/eii-controller-diag.csv '' '' 'HE TE' > validation/controller-diag-testhelp.txt
```

Tests check the 8-state/9-slot periods, delayed carry STATE0/STATE3,
old-state input capture, partial unknown inputs, asynchronous reset scope,
all-startup-phase consensus against 16 concrete initial-phase runs with
varying data and resets, exact half-edge counts and service selection wiring.
Native suite and timing ASan/UBSan results are captured. No new Windows
build/runtime verification or hardware timing measurements.

## Continuation in checkpoint 0.10

Actual supplied PROMs, their microinstruction latches, request/read pipeline,
delayed UCD08.2 acknowledge and a CPU request/completion/wait observer now
connect to this timing core. See CLOCKED_PROM.md for conservative unknown
handling and the distinction between observation and actual DRAM/WAIT service.
