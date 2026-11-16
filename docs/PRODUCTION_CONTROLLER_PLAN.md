# Concrete controller assembly (production step 4)

Steps 1..4 are implemented. Steps 1..3 are proven per component; step 4 is
proven for the fully known path:

| component | file | proof |
|---|---|---|
| register file + staging | production_registers.h | 900k ops |
| 20-bit ALU | production_alu.h | 900k ops |
| RFD bus + IC141/148/152 | production_bus.h | 900k ops |
| DRAM port | production_dram.h | 200k steps |
| CPU request pipeline | production_request.h | 400k ops |
| DAC registers | production_dac.h | 100k ops |
| upper path | production_upper.h | 200k risings |
| full assembly | production_controller.h | 20000 complete slots |

## Assembly status

`ConcreteController` is wired and validated for the single, fully known path by
`tests/production_controller_tests.cpp` (also in `tools/build-native.sh` and
CMake, with `EII_DIAGNOSTICS=1`). The test starts the reference from a fully
known state, asserts `capturedKnownForTest() == 31`, and then compares control
value/known, phase, channel, shift, captured, pipeline, request, ack, the
lower-PROM address, RFD data pipeline, ALU output/clock and all 64 register
cells over 20000 rising/falling slot pairs. It is a Win64 LLVM-MinGW build with
zero warnings; the native run and a UBSan run pass.

The concrete model covers only the case with exactly one known RFD driver;
floating buses, SRAM/ALU contention and partial-known masks stay on the
reference consensus path and are deliberately not represented. The slot test's
synthetic PROM is constrained to that contract, and it seeds an idle IC92 write
port (the unknown power-up handshake otherwise makes the ninth-slot write window
uncertain).

## Runtime integration (validated)

`ControllerTiming` now has an optional production engine (`enableProductionEngine`):
`initializePowerOn(seed)` fills the concrete controller from the same bit
sequence as reference path0, and every runtime edge is routed to
`production::Controller` with the observation surface copied at CPU boundaries.
`Machine::enableProductionController(true)` selects it; the reference remains
the default. The engine has no reset/unknown-state fallback: any unknown CPU
pin, staging byte, DRAM byte, floating/contended RFD bus or power-on UCD04
ambiguity sets `invalid`, and the caller must not use the state.

The GUI's opt-in concrete startup (`FrontPanelSession`) now boots phase0 with
DRAM seed1 and controller seed `0xe1122026` on the production engine. The
reference override uses the same seeds but stays on the reference engine, so it
is a like-for-like oracle.

Evidence (2026-10-19, original main/scanner/PROMs, `18 Grand Piano #2.hfe`):

* 4M ticks: production vs reference full output identical; 100% of rising edges
  took the concrete path; `invalid=0`.
* 200M ticks, edge route: identical full output (report, all64 RF cells,
  memory/known/scanner FNV, PC, DRAM counters); `P01 GrandPiano 1`; SHA256 of
  both stdout captures is `3a41e7fa38510e4177027d164fd1f7e7362ad0f7a28e351976766d923f605213`.
  Production 81.95s vs reference 110.46s on this host.
* 200M ticks, normal (event/device/GUI) route: `CONTROLLER_COMPARE EQUAL`,
  `invalid=0`; production 59.89s vs reference 66.33s.

Reproduce with `tools/production_controller_probe.cpp` (assets supplied by the
caller; never committed). The reference stays the verification default, and
the production engine is selected only for the explicit concrete startup.

## Wiring order (already implemented)

`ConcreteController::rising(requests, flags)` mirrors
`ControllerTiming::risingClockImpl` for the single, fully known path. Order that
must be preserved (taken from the reference):

1. data pipeline outputs (low20, flags, ucd4b, ucd9) from the previous edge;
2. ALU sum from `low20 + A + CI` (ucd3/ucd4a/ucd4b), carry into bit23;
3. DRAM rising through `upper.dram` with the old control word;
4. write-port settle, `-MPWEN`, staging/selected address, RF enable;
5. RFD bus from the three possible drivers (staging, RF, ALU output);
6. IC140 flag capture from RFD23..20;
7. ALU output latch clock (old pipeline, simultaneously clocked);
8. low-aperture register write commit;
9. upper PROM address (old upper bits, phase&7, pipeline, channel bit3,
   captured request) and lower PROM address, both O(1) table lookups;
10. `upper.rising` with the new word, ack/delayed9 from the old latch;
11. data pipeline clock with the new control bit4 and bit9;
12. pipeline and captured updates, channel carry, phase advance, write-port
    settle for the new channel.

`falling(control)` is the reference's falling: ALU address clock, channel and
read clocks, then `upper.dram.falling`.

## Scheduler (after the controller is proven)

Replace per-edge stepping with integer deadlines: the next event is the earliest
of the CPU bus change, a timer/request edge, the slot boundary and the span end.
Between deadlines only the counters advance. The worker then sleeps to its
deadline with a waitable timer (the Emulated-3 pattern), which is what turns
"runs at 1.9x real time" into "uses a few percent of a core".

## Validation contract

`ConcreteController` must reproduce `ControllerTiming` over complete slots for
the same seeded PROMs, starting state and CPU bus sequence. The asset-free slot
test compares the control word (value and known mask), channel, phase, shift,
captured, pipeline, request, ack, the lower-PROM address, RFD data pipeline, ALU
output/clock and all 64 register cells. Wait level, DRAM observations, DAC codes
and CPU bus service are covered by the seeded-boot probe instead, which matched
the reference over both 4M and 200M ticks and over the normal event/device route.

The fully known single-driver precondition still applies, and the engine has no
unknown-state fallback. Selection is limited to the explicit concrete startup;
the verification/GUI default and all16-phase mode stay on the reference.

Remaining before calling this a general runtime: unknown-state fallback or
rejection policy in the worker, independent performance characterization, and
extending the asset-free test to valid bus/DRAM service sequences.
