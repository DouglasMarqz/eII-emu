# Runtime redesign: experimental, not a release

The performance goal is **not met**. Main remains the user's working 0.35.
Do not call the present branch a real-time instrument, ship another GUI build,
or treat small interpreter speedups as the requested architectural fix.

## Reference architecture inspected

Tuth/Emulated-3, commit `4028d1496f5b27fdcda3e4a4c6ecd5161504a8af`:

* `runtime/eiii_runtime.cpp`, `run_until_cycle`: CPU instruction budget ends at
  the next device timer, PIT output change, or caller boundary. Device writes
  can abort the current slice and retarget the deadline.
* `runtime/eiii_machine.h`, `pit_next_event/pit_update/pit_w`: counter state is
  advanced arithmetically to event/read/write boundaries.
* `fchip/eiii_fchip.cpp`, `advance`: derive sample steps and retained phase from
  elapsed clocks/divisor; skip inactive lanes. This is functional hardware
  execution, not simulation of every internal source-clock gate.
* `host/eiii_backend.cpp`: render to timestamp before RF/control/CV changes;
  process the analog bank at output-rate times oversampling, not 20 MHz.
* `cem/eiii_cem3387.cpp` and `_v2.cpp`: keep coefficients until their actual CV
  inputs change. The worker uses a bounded audio ring and sleeps when ahead.

These are source observations, not a locally measured EIII CPU benchmark.
The EIII scanner interface and device details are not substitutes for the EII
scanner firmware, bus protocol, DRAM arbitration, or reconstruction filter.

## Current experimental components

`edge` is the reference; `event` adds lazy channel timers; `devices` also uses
CTC timer deadlines, physical FM edge processing, floppy event state, SIO/PIO
IRQ invalidation and the DRAM strobe executor. `concrete` additionally executes
a single known controller control trajectory directly. Unknown words retain
their masks; unknown control histories fall back to the reference algorithm.
This is still a **source-clock controller interpreter**, not yet the required
functional transaction scheduler. `concreteSteps` measures coverage only.

`DramStrobeProgram` lowers known electrical inputs to row/column/write actions.
It retains the original edge timestamps and all actual memory accesses; it
does not inject sectors, shorten WAIT, or select unknown data. Unknown pins use
the original network. The strobe-only trial was performance-neutral.

`realized` separately selects reproducible pre-first-clock digital SRAM/FF and
divider contents. This is an explicit experimental initial condition, not
measured factory reset values, not an optimization, and not the GUI default.
Repeat initialization and rejection after clocks/programming are tested.

Rejected: lower-pipeline memoization plus DRAM quiescence caching. Although
differential tests passed after fixing an address-latch transition bug, the
24M trial regressed from about 10.4 to 11.2 seconds. Both caches were removed.
Do not reintroduce them as an architectural solution.

## Remaining structural work

1. Compile the lower PROM sequence to timed RF/ALU/sample transactions. Retain
   sample-address capture, DRAM read, CHDB latch and DAC ACK as separate events.
2. Preserve the ninth controller slot: even without voice requests it executes
   background RF/ALU operations. Skipping it changes controller state. Derive
   its bulk state transformation from the supplied PROMs and verify it against
   the reference before suppressing clocks.
3. Use CPU bus/WAIT/IRQ/DMA deadlines to stop device work between observable
   events. Two Z80 cores are not a justification for repeated whole-machine
   peripheral dispatch on every T-state.
4. Replace the production audio observer's 50 ns integration loop with a
   timestamped DAC/CV/event input to an independently sampled analog engine.
   The existing observer explicitly disables timer event mode while active.
   Preserve it for laboratory checks, not the real-time rendering path.

Acceptance must include original Grand Piano HFE -> physical read separator
-> SIO -> DMA -> strict DRAM -> genuine firmware LCD, the sample/DAC event
sequence, WAIT/IRQ ordering, and clean serial performance runs. A passing
register test alone is insufficient. Keep main and Windows releases unchanged
until there is useful performance headroom for the complete audio path.

## 0.36c investigation: no architectural breakthrough

Three opt-in trials were tested and removed from the production source:

| Trial | 24M baseline / trial wall seconds | Result |
|---|---:|---|
| Batch certified quiet rise/fall pairs inside each CPU boundary | 10.2881 / 10.7625 | Slower; 7,814,026 clocks covered, only about half their individual executions avoided |
| Invalidate held RF2/pipeline transfers by datapath changes | 10.0251 / 10.4075 | Slower despite 30,481,102 held transfers |
| Precompile all ternary lower-PROM slot programs | 10.2435 / 11.1028 | Slower; per-clock engine and additional dispatch remain |

These are exploratory single-pair measurements, not a release benchmark.
The full 24M reports agreed in every trial. Quiet-pair random/actual-PROM
controller validation covered 320,000 CPU boundaries, including ASan/UBSan.
The compiled-PROM trial also checked every captured value/mask/phase against
the ternary lookup. Removed prototypes are not features of the current source.
Do not repeat these guard/cache experiments as the proposed runtime rewrite.

The retained change is an exact algebraic replacement for the partial-known
ALU's twenty-step carry recurrence. For a fixed addend, carry into bit k is
monotone in operand bits below k. Compute the smallest and largest compatible
operands and extract their carry vectors using `operand XOR addend XOR sum`.
Matching endpoint carries are certain. A sum bit is certain only when both
its operand bit and its carry-in are certain. Bit 20 carry maps to RFD23.
Representative unknown value bits retain the original serial recurrence's
values, not only its known-mask observations.

404,976 frozen-serial-recurrence comparisons cover all eight hardware addends
across exhaustive eight-bit ternary patterns with low/high carry contexts,
plus arbitrary 20-bit masks/values and 12-bit constants. Existing independent
exhaustive controller-ALU tests remain in the native suite.

The clean 24M ABBA comparison averages 10.2947s before and 10.2674s after;
individual timings overlap. **No convincing whole-machine speed gain is
established.** Both remain approximately 0.58x real time. This arithmetic
change is not the requested event-driven emulator. No GUI release or main
promotion is justified. See `validation/runtime-036c-*` for final checks.

The useful next boundary is a sample transaction with separately scheduled
address capture, CAS read, CHDB transfer and DAC ACK, plus the ninth-slot
background RF32 increment/CPU-staging window. Event eligibility must be
established when a transaction starts, rather than rescanned at every CPU
boundary. Merely lowering PROM lookups while retaining the source-clock
interpreter demonstrably does not solve the problem.

## 0.36d functional lower transactions

An opt-in lower voice transaction and lazy observation bridge now exist; see
`VOICE_TRANSACTIONS.md`. This evaluates timed RF/address actions from loaded
PROM programs, including unknown masks, with entry-only eligibility and an
analytic idle program. It does not pre-read DRAM. The upper source-edge
arbiter, ninth slot and CPU/peripheral adapters remain. Final24M ABBA shows
about4.4% less wall time, still only0.573x realtime; goal remains unmet.
The exact earlier EIII Windows500us pacing fix was recovered and its reusable
waitable-timer implementation re-read; it does not explain the EII native
unpaced computational budget. No Windows runtime result is claimed here.

## 0.36e production routing audit

The GUI worker had never enabled the accelerated presets used by benchmarks.
The shared normal preset now fixes that wiring defect. Actual worker reports
match through the full 200M-tick bank load. Clean 24M wall drops14.9321 to9.9312s,
but remains0.604x realtime. See RUNTIME_ROUTE_AUDIT.md. The remaining large target
is upper-controller/DRAM/WAIT event scheduling, not small arithmetic helpers.
