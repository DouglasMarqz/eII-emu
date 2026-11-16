# Active DRAM transactions and the ninth slot (plan after 0.41)

0.41 collapses certified **idle** controller intervals. The remaining cost is the
active path: CPU and sample DRAM transactions and the ninth slot's background
and CPU-staging work, all still evaluated at source-edge cadence.

## Measured starting point

`tools/runtime_cost_probe` with the documented LLVM-MinGW 20261006 build
(`-O3 -flto -DNDEBUG -DEII_DIAGNOSTICS=0`), concrete startup phase0, RAM seed1,
normal runtime, runtime audio attached, OS3.0 image, 24M CPU ticks:

```
halfClocks=120000000  sourcePeriods=60000000
idleSpanPeriods=24531899 (40.89%)   edgePeriods=35468101 (59.11%)
wall=6.97s   ticksPerSecond=3443486   realtimeRatio=0.861
LCD [This Will Take  ] [A While         ]   DMA=55558
```

## Why the spans are refused (measured)

The same run counts every refused `advanceQuietClocks` attempt:

```
spanAttempts=39037225
voice inactive =12438724 (31.9%)
voice not idle = 6508994 (16.7%)
voice phase0..9= 4286407 (11.0%)
control word   =  104469 ( 0.3%)
request/pipeline=  129506 ( 0.3%)
DRAM port      =       0
```

So the active CPU/sample DRAM transaction is **not** the dominant cost: request
and DRAM refusals together are 0.6%. The target is the voice-slot coverage:

1. **No active voice transaction (31.9%)** — the interval is not in a certified
   voice slot at all, most likely the ninth slot's background program, which the
   0.36c notes already proved does RF/ALU work and cannot be skipped.
2. **Voice program not idle (16.7%)** — requested programs do real RF/address
   work and need timed transactions rather than skips.
3. **Phases 0..9 (11.0%)** — the early phases of a slot, where the lower
   pipeline is active.

The plan below is therefore re-prioritised: ninth-slot and non-idle voice
transactions first, CPU/sample DRAM transactions later (they are only 0.6% of
the refusals but remain required for correctness at those points).

## Where the remaining 59% actually sits (0.42 measurements)

Two probes settle the question:

- The ninth slot requests control words `4a(?) 6b 48 ec 48 e04a f84a f80c` in
  phases 8..15 and contributes only316408 edge samples per12M ticks, so it is
  a small slice, not the bulk.
- Trying to start the idle span at phase8 instead of10 keeps the oracle test
  green but changes nothing: the refusal simply moves from the phase check to
  the control check, because phases8..9 ask for the background sequence and not
  the canonical `4a`. The change was reverted rather than left as dead code.

The dominant block is therefore phases0..7 of every slot: the voice transaction
is entered at phase8 and covers8..15, so the first half of each slot has
`voiceActive=false` and runs the complete edge machinery. Extending coverage
there means a full-slot transaction (phases0..15), not another guard tweak.

Order of work is updated accordingly:

1. Full-slot voice transaction (phases0..15), including the ninth slot's
   background sequence, validated against the edge oracle over complete slots.
2. Only then the CPU/sample DRAM transaction work (0.6% of refusals).
3. Keep every instrument behind EII_DIAGNOSTICS and Release free of them.

## What makes an interval non-quiet

`ControllerTiming::advanceQuietClocks` refuses a span unless all of these hold:

- the lower voice transaction is certified idle (canonical 4A/phase8 entry);
- control pins are definite, the channel request is low;
- the CPU request pipeline is inactive/completed;
- the DRAM port proves no strobe or latch-clock edge can occur.

During a boot the CPU executes from DRAM (0x2800..0x7ffff), so almost every
interval has an active CPU request. Those intervals are the 59%.

## Proposed transaction

Mirror `VoiceTransaction` (0.36d) for the upper sequencer:

`UpperTransaction::compile(knownControl, feedback, requestLevel, startPhase)`

- walks the two loaded PROMs from the known starting state, using the same old-
  state feedback rules (UCD15/14 from the previous latched word);
- stops at completion (CPU WAIT release), at the next CPU-visible observation,
  at a CPU-staging write, or at ninth-slot entry;
- emits timed actions with half-clock offsets: RAS, CAS, WE, IC69 address
  latch, IC103 read latch, CHDB capture, DAC ACK, WAIT assert/release;
- keeps partial-known masks; any unknown bit that can change the outcome
  cancels the transaction and materializes the reference state.

Eligibility is decided at transaction start, exactly like the voice path:
known CPU pins, known controller state, loaded PROMs, no mode change, no reset.
Cancellation must return the exact reference state at the current half-clock.

The ninth slot cannot be skipped: its background sequence does RF/ALU work even
with no channel or CPU request (see memory 0.36c). It therefore needs its own
short transaction with an interruption point at the CPU-staging write window.

## Order of work

1. Instrument the active intervals (counter split: CPU request, sample read,
   ninth slot, other) to know which one dominates. No behaviour change.
2. CPU DRAM read/write transaction compiler plus differential test against the
   source-edge oracle over randomized intervals.
3. Sample-read transaction and CHDB/DAC ACK timing.
4. Ninth-slot background transaction with the staging interruption.
5. Re-run the probe, the full native suite and the 200M original-bank
   comparison; keep the reference engine as the oracle and default.

## Rules

- No skipped bytes, no WAIT shortening, no forced known values, no firmware or
  media patch.
- Every transaction must be proven equivalent to the edge reference for the
  same starting state, including unknown masks and counter state.
- `Machine` raw construction stays the reference; `enableNormalRuntime()` is the
  only place these transactions may be selected.

## The real entry blocker (measured 0.42)

Counting the voice-transaction entry at phase 8 by reason, 12M ticks, OS3.0:

```
entry attempts at phase8 = 3016912   (every one presents low control 4a)
blocked by control.known != 0xFFFF = 276862
blocked by writePort.pending unknown = 39361
blocked by branches.size()!=1        = 198
compile rejected                     = 17
shift/flag/alu/ucd4 blockers         = 0
```

So the transaction is not blocked by the control *value* (always 4a) but by
**unknown bits in the control word**, inherited from partially known inputs
(captured flags, pipeline, read-inactive) that come from the still-unknown DRAM
power-on data. In other words the remaining 59% of edge-driven time is largely
the price of the symbolic uncertainty model itself: while state contains
unknowns the span cannot be certified, so the reference path runs.

Consequence for the architecture: pushing the reference engine much past its
current 0.86x real time means either reducing modelled uncertainty (more seeded
state, which is a documented simulation condition, not hardware evidence) or
building the separate concrete production mode. Guard tweaks and extra slot
coverage cannot fix it. The idle-span and transaction work stays valuable, but
it is no longer the main lever.
