# Filter restart investigation, 2026-10-21

Baseline release source: `d524d445ab22319556ae1062d04eddda6f1c59c9` (0.67).
The user observed about7% ->5% process CPU after Legacy -> nonlinear switching
while Play remained active. Live serial had no perceptible effect on this
Windows observation. Its0.67 bounded-view correction remains useful, but is
not established as the cause of this Play symptom.

## Supplied inputs and real firmware execution

The newly accessible HFE was validated read-only:80 tracks,2 sides,160 sectors
with valid CRCs,312kbps HFE header. Filename wording about a corrupt preset is
not evidence of sector corruption. HFE SHA256:
`c84ca9a132c31cce61520006dda9d22a957533f12361f89773b90cc842e3c1b9`.
The supplied complete calibration parsed successfully; profile SHA256:
`1a92e308dba077e3a47de942002b219b73481a8629a761256bf25ea4262cc167`.
Neither file nor decoded sample data is part of the source commit.

Native seeded production boot reached P01 Marcato Strg,520552 DMA transfers,
then physical scanner Play contact started S01 La Primavera /90 BPM. Capture
ended with production invalid=0. No firmware patch, reset or host note
substitution was used to start the sequencer.

## What switching resets, from the implementation

`FrontPanelSession::apply(AudioModel)` calls `Machine::enableRuntimeAudio`,
which destroys the previous runtime engine and joins its DSP worker. It creates
a fresh bank/worker, reapplies the same stored calibration, and clears the host
PCM queue. This also discards that engine's pending DSP blocks, state histories,
resampler history and statistics. Firmware, DRAM, voice allocation and hardware
CV values are retained. Thus a switch changes several host states, not just a
single filter equation; an overall process percentage does not identify which
change mattered.

The inspected worker destruction does not detach or accumulate old DSP
threads. The worker releases its mutex before solver rendering and before
calling the PCM sink. These source observations do not prove absence of all
Windows scheduling or contention problems.

## Controlled DSP comparison

`tools/filter_restart_probe.cpp` captures480000 physical pin snapshots over10
simulated seconds using public Machine observations at exact48kHz boundaries.
Each contains eight sample codes, ACV, FCV, QCV, knownness and channel0 playback
mode. The pin-sequence FNV-1a digest is`11b37885189adcce`. Every DSP pass receives
that identical sequence with the supplied profile and96kHz internal rendering.

Four passes alternate continuous rendering and fresh bank creation after5s.
The restart represents the DSP-state part of a model switch, not the Windows
thread or queue behavior. Creation cost is outside timed windows. Second6 is
the first second after restart; seconds7–10 exclude that settling interval.

| Both repetitions, seconds7–10 | Continuous | Restarted |
| --- | ---: | ---: |
| Solver samples | 6,144,000 | 6,144,000 |
| Solver iterations | 13,846,334 | 13,846,336 |
| Fallbacks / held / nonfinite | 0 / 0 / 0 | 0 / 0 / 0 |

For seconds6–10 the restart saved280 iterations in17,184,246 across both
repetitions (about0.00163%); that small settling difference vanished thereafter.
There is no persistent solver-work reduction matching a7-to5 process-CPU
change in this run. An iteration count is not a complete CPU-cost model.

Per-second thread-CPU times varied substantially in this unpinned container;
they are logged for reproducibility, not offered as a Windows speedup claim.
After excluding second6 the respective mean times were0.750213 and0.720929 CPU
seconds per audio second. This uncontrolled timing difference cannot establish
a state-reset benefit. Other nonlinear per-step work, CPU frequency and host
scheduling are not controlled by counting Newton iterations alone.

## Scope and next discriminating evidence

The test covers the supplied bank/profile and this10s sequence excerpt at96k.
It does not cover a long warmed session,192k rendering, the user's entire
interaction history, Windows MMCSS/thread migration, host queue contention,
WASAPI or model-switch transients. Pin capture intentionally uses many short
Machine::run calls and has no host audio consumer: its machine wall time is
not a production performance benchmark. Captured inputs are replayed off-line,
not through the asynchronous DSP worker.

No production engine/filter/audio behavior is changed in this checkpoint;
there is no new executable release. Do not insert periodic filter resets:
those alter oscillator/RC histories and are unsupported by this evidence.

The next useful Windows evidence is paired manual captures about5s apart while
the high-cost state is present, then a Legacy -> same nonlinear-mode switch
and another pair after settling. The thread rows, HOST ENGINE owner-thread-busy,
HOST CHIP solver deltas/load/queue, audio interval counters and selected sample
rate separate owner cost, solver work and scheduling/transport behavior. A new
thread's first interval may have busy=-1; compare its subsequent pair. This
request is about attribution, not permission to continue investigating.

## Reproduction

Build `filter_restart_probe` through CMake or compile it with the same release
core objects, C++20/O3/NDEBUG/EII_DIAGNOSTICS=0/pthread. Invoke:

```
filter_restart_probe ROM-folder bank.hfe calibration.txt
```

Media/profile paths are caller supplied. The tool is read-only and prints no
sample payload. It retains the temporary pin tape in RAM only. Executed results
are in`validation/filter-restart-audit.txt`. No target Windows runtime test or
complete resolution of the reported7% ->5% change is claimed.
