# Experimental functional voice transactions (0.36d)

This is a lower-controller transaction implementation and oracle bridge, not
a complete real-time emulator. The unreleased 0.36e GUI worker now selects this implementation through
`Machine::enableNormalRuntime()`, shared with the `voice` benchmark engine.
Raw Machine construction remains the reference engine. No main-branch or
Windows/audio release is promoted. See RUNTIME_ROUTE_AUDIT.md.

## Execution boundary

`VoicePrograms` derives all 243 ternary request/flag programs from the loaded
lower PROM once. It marks a program usable only when every instruction is
known. The canonical voice entry (old lower control 4A, phase8, held linked
latches) is certified against the loaded PROM, not a firmware hash shortcut.
An uncertain request/control or unsupported entry retains the reference path.

`VoiceTransaction::compile` receives four RF words and an incoming ALU value.
It has **no DRAM view or memory-read interface**. It evaluates one voice slot
as a pure transaction and produces timed RF-write/address/ALU-capture events.
Values retain partial-known masks and their representative bits. Each phase
uses old pipeline/ALU values before simultaneous captures and writes. Event
half-clock offsets0..14 represent the eight100ns controller rising edges;
coincident events must settle as one group before external observations.

The certified idle program is evaluated directly: RF2 enters the pipeline,
the20-bit RF2+1 result (with board flag/carry mapping) enters the ALU while RF3 enters the pipeline, then RF2 is held. Three
virtual observations cover the eight phases. No RF/address write is invented.
Generic requested programs retain their exact instruction-derived actions.

The `ControllerTiming` bridge consumes the transaction at the original source
boundaries. Actual RF writes occur at those boundaries. The upper sequencer
still captures the address, performs the actual RAS/CAS memory read, captures
CHDB and clocks DAC ACK at its original times. Compiling an address never
pre-reads a future sample; intervening CPU/DMA writes remain visible.

Eligibility is established at entry, including known CPU handshake state.
Unknown CPU pins cancel at `driveCpuBus`; reset, PROM/DRAM-path replacement,
mode changes, and missing low write apertures materialize/cancel the pending
transaction before returning to reference execution. Known CPU request/read
changes may continue: the upper sequencer remains live and they cannot alter
voice RF through the ninth-slot-only CPU write window.

Private lower pipeline/ALU snapshots are deferred while a transaction runs.
Public observations read the appropriate virtual beat; observation helpers,
fallback boundaries and slot exit can materialize the exact reference state.
There is no full-controller state comparison/cache or selected unknown history.

## Limits and next boundary

The upper arbiter/DRAM kernel and CPU/peripheral adapters still execute at
source/T-state cadence. The ninth slot remains on the previous concrete or
reference implementation; its RF32/background and CPU-staging window are not
skipped. This is why functional lower transactions alone are insufficient.
Next: schedule upper/DRAM/WAIT events to actual deadlines, retain ninth-slot
background operations with interruption at CPU-staging writes, then consume
DAC/CV events in an independently sampled production audio engine.

`voiceTransactions` counts controller service slots, including idle slots;
it is not an audio-sample count. Runtime integration currently uses local
packet phases; a future general event dispatcher must supply the global
half-clock epoch when directly consuming the exported event list.

## Validation

* All captured value/mask/phase combinations of the program bank vs ternary
  PROM lookup, including generic synthetic programs and original external PROMs.
* 6144 functional transactions /32576 timed events against the source-edge
  reference with original PROMs; raw RF, pipeline, ALU and address values agree
  at every boundary. Default asset-free fixtures also exercise generic code.
* 200000 randomized controller clocks compare full future state/SRAM/DRAM/DAC
  and counters, including sparse31-clock observations, unknown CPU/request
  inputs, mode switches, PROM reload/reset and missing falling/write apertures.
* ASan/UBSan and final native/real-bank/unknown-all16 checks are recorded in
  `validation/runtime-036d-*`; see provenance for exact final-source results.

Clean final24M ABBA run: concrete mean10.94201495s, voice mean10.46305655s
for6 simulated seconds (about4.4% less wall time,0.573x real time). Reports are
byte-identical. These are container wall timings, not Windows CPU usage. The
performance goal is still unmet; do not promote this as a usable instrument.

## Earlier EIII pacing fix

The named prior conversation "Portolás WIN64 VST3" identified a Windows
MinGW500us wait truncated to zero, causing active polling. Re-read pinned
`Tuth/Emulated-3@4028d149`, `runtime/eiii_short_wait.h` and `eiii_engine.cpp`:
the fix reuses a Windows waitable timer, with a nonzero rounded Sleep fallback;
the full audio-ring case calls `idle_wait.wait_us(500)`.

The current EII worker uses command-interruptible condition-variable waits,
not that nanosleep loop. Its unpaced native benchmark is itself slower than
real time, so host pacing alone cannot remove the measured emulation cost.
No native Windows waiting/CPU measurement was performed in this checkpoint.


## 0.41 interval execution

Certified idle lower transactions now permit real upper/DRAM quiescent spans,
limited by CPU and timer boundaries. Logical clocks and complete future state
are retained. This is a partial event engine: active CPU/sample transactions
and the ninth slot remain edge-driven. See `RUNTIME_041.md` for proof conditions,
ABBA measurements, exact scope and remaining performance limit.
