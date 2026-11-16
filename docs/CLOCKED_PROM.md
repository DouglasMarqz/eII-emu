# Clocked micro-PROM and CPU request observer (checkpoint 0.10)

The user-supplied IC135/IC137 images now clock inside ControllerTiming.
The existing PromLatch remains the independently tested direct lookup kernel;
ControllerTiming owns the sampled per-trajectory state and runtime tables.
No firmware bytes or precomputed firmware tables are embedded in source.

## Evidence and edge order

Visually inspected service PDF 73/75 (CPU handshake and memory decode),
81 (micro-PROM), 84 (pipeline), 93 (clock/reset), including enlarged traces.
+ C10MHz.1 and + C10MHz.2 are in-phase ideal buffered source clocks;
IC89's input at IC134 pin9 has R27 pull-up. Programmable clock-inhibit
behavior, physical buffer delay and edge hazards are not modeled.

Each modeled source rising edge snapshots these OLD inputs simultaneously:

1. IC136/138 sample both PROM outputs using old UCNT, CHSL3, IC140 captured
   request/flags, IC152 captured request/read and UCD15/14 feedback.
2. The modeled portion of IC152 captures -MPREQ -> -MPRQ.1 and -PRD ->
   -MPRD.1, plus OLD UCD08.1 -> UCD08.2.
3. The existing gated IC140 and counter/shift updates use their old inputs.
4. The newly latched +UCD09.1 can clock the CPU completion FF. Its result
   affects the next request-pipeline capture, never that same edge's capture.
5. UCD08.2 reaches the already connected rotated output-board ACK decoder.

The one-clock delay from UCD08.1 to UCD08.2 is required by IC152. Sending
the new PROM latch directly to the ACK decoder would acknowledge too early.
The remainder of IC152, other RFD pipeline latches and datapath ALU/register
files are not implemented. In particular, this is not a full pipeline model.

## Partial inputs and startup

Both 74S374 microinstruction latches and the request pipeline start unknown;
they have no CRST clear. Each of the existing 16 startup-phase trajectories
retains its own control known-mask, input pipeline and CPU handshake state.
Only outputs known equal across all trajectories are exposed as known.
Unknown inputs are never replaced with zero or an invented instruction.

PromConsensus compiles a runtime 3^9 = 19683-entry consensus table per loaded
chip. Each input can be known low, known high or unknown. Every table result
is the equal known bits over all compatible 512-address ROM values. Missing
chips produce unknown outputs independently; reloading replaces the runtime
table. This avoids enumerating all addresses on every simulated edge.

Masks conservatively lose correlations between partially known feedback bits,
between different input latches, and with the shared channel request model.
The table resolves the resulting superset of possible addresses. It is not
an exact symbolic simulation or physical startup-phase measurement. Fixed-phase
fixtures can establish a deterministic test sequence without claiming that
Machine's power-up phase is known. RFD23..20 remain unknown in Machine.

## CPU handshake observation

The 7-12 decoder requests controller access for memory cycles outside local
ROM/SRAM; Machine derives this from MREQ and physical address >= 2800h.
Sheet 7-10's IC61 first FF has D and /PRE high, clock UCD09.1 and
/CLR = !(-MPRQ). An inactive request asynchronously clears completion.
The PROM-facing -MPREQ is completion Q OR -MPRQ. IC152 registers it and
-PRD independently; read polarity is an electrical level, not an asserted flag.

The second IC61 FF samples !completion AND !(-MPRQ) at the CPU-clock
boundary. Observed -PWAIT = !waitQ AND external -IWAIT (default high in the
current host). Tests cover completion-edge, clear priority and delayed wait
release. CPU-to-source phase, metastability and asynchronous transitions
remain idealized.

This handshake is an **observer**: it feeds PROM request inputs, but its WAIT
output does not stall the actual Z80, and memory reads/writes still use the
existing direct memory model. No DRAM bus ownership, committed transaction,
CPU read-data latch, sample fetch, DAC update or audio completion is claimed.
Actual WAIT integration must arrive with register files and DRAM service.

## Runtime assets and validation

The original ZIP was recovered and both 512-byte images matched the existing
ROM catalog exactly:

| Image | SHA-256 |
| --- | --- |
| IC135 | 725cd24f9e98f201efcd7cfac4e07f38713495fa960b3f37664e76bd079927f5 |
| IC137 | 874b9ec1214977fb95fa77b337b7cb254d243c9a143547af3b440cd2d9ff9116 |

No assets are committed. When images are omitted, their outputs remain unknown
and PROM-clock count is zero. Supplying one image does not invent its partner.

```sh
bash tools/build-native.sh > validation/tests-clocked-prom.txt
g++ -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Ivendor/chips src/chips.cpp src/machine.cpp tests/controller_prom_tests.cpp -o build/controller_prom_tests_san
ASAN_OPTIONS=detect_leaks=0 build/controller_prom_tests_san > validation/sanitizer-clocked-prom.txt
build/eii_probe ../roms/main.bin ../roms/scanner.bin 40000000 /tmp/eii-clocked-prom-main.csv ../roms/ic135.bin ../roms/ic137.bin > validation/prom-main21-scan31.txt
build/eii_probe ../roms/diag.bin ../roms/scanner.bin 4000000 /tmp/eii-clocked-prom-diag.csv ../roms/ic135.bin ../roms/ic137.bin 'HE TE' > validation/prom-diag-testhelp.txt
```

Tests exhaustively compare all 19683 partial PROM input patterns against full
address enumeration for a synthetic image; verify missing/reloaded images,
old request-pipeline timing and electrical A3/A2 mapping, simultaneous old
counter/feedback lookup against PromLatch over scan wrap, startup feedback
uncertainty, CPU completion/wait FFs, and the delayed PROM-to-ACK path.
Native eight-target suite and ASan/UBSan results are recorded. Runtime logs
use original main/scanner/diagnostic firmware with both supplied PROMs.
No new Windows build, full OS boot, integrated audio or bench calibration.

Original main2.1/scanner3.1 with both supplied PROMs retains Insert Diskette
at40M CPUticks,100M PROM clocks. Diagnostic HE TE retains Static RAM OK
and help at4M ticks. Final reported control known-mask is1536 (0600h);
the other bits remain unresolved across startup trajectories and unknown
datapath inputs. Representative value330 must not be read as a fully known
microinstruction. Observed WAIT_n is1 at the final idle snapshot; it does
not certify completed memory arbitration.
