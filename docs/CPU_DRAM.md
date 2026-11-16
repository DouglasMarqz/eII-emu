# CPU DRAM service and WAIT adapter, checkpoint0.16

Scope of the initial0.15/0.16 work: CPU reads/writes through the existing512KB
physical address model. Checkpoint0.17 extends the same port with playback
reads and IC40/channel DAC registers; see SAMPLE_TRANSPORT.md. Sample
recording, calibrated audio and complete OS boot remain absent.

Visually verified service7-18 and7-23..25 (PDF81,86..88). CPU physical00000..
01FFF remains ROM,02000..027FF local SRAM; all other CPU RD/WR cycles can use
this service. Both supplied PROMs automatically enable it in Machine::load.
Without both images the existing direct-memory probe mode remains explicitly
reported. Tests can enable the CPU service with a synthetic PROM.

## Address and strobe path

UCD15.1 low selects CPU address, high selects ALUGO-latched SUM18..0. UCD11.1
selects low/high address byte; IC69 clocks the selected MAD byte on rising
+C10MHz.2. IC96/97/10 capture SUM on NAND(UCD07.1,-C10MHz.1) rising edges.
The CPU bank is physical18..16. A resolved RAS row then CAS column reconstruct
bank*65536+column*256+row. CPU bank setup is stable across the request;
flat eight-bank service covers main/expansion address selection, not separate
option-board propagation/RC calibration.

IC49 negative-edge JK has J=!UCD12,K=UCD12, thus Q=!UCD12 after source falling.
IC68 positive-edge DFF samples OLD UCD12 on source rising. +RAS is ACTIVE
HIGH: NAND(IC49.Q,IC68./Q), before bank NAND inversion to -RASBn. +CAS is
IC68.Q AND !UCD13; the subsequent inverters produce active-low -CASBn.
UCD10 high produces active-low -WEBn. CPU writes require fully resolved
bank/row/column. CAS latches the address; WE can already be active or assert
later while CAS remains active. The supplied PROM uses this late-write path.
Unknown, page/refresh and sample cycles are not invented as CPU transactions.

## Read latch and core adapter

IC141 delays UCD09.1. IC103 clocks on rising -MPGTE=
NAND(-C10MHz.1,delayed UCD09). It samples OLD MDO before same-edge CAS/RAS
changes. CPU read data therefore comes from IC103, not a direct memory lookup.
CPU writes are deferred until the matching CAS/WE event, then committed once
only when all16 startup trajectories agree on address/data. Each path has its
own row/column, strobes, MDO and IC103 state; the backing store is shared.

CpuRequestHandshake supplies the existing hardware WAIT observer. The CPU
adapter conservatively extends a DRAM cycle while WAIT is asserted/unknown,
while any trajectory is not serviced, or while read-data consensus is unknown.
These readiness guards may add hold clocks compared with a characterized
single-phase physical machine. They prevent consuming stale/unknown bytes;
they are not additional hardware gates shown on the schematic.

Pinned floooh/chips z80.h tests _wait BEFORE _mread/_mwrite emits the new
request pins. Merely setting WAIT after observing that request cannot stop the
next internal data-consumption step. Machine therefore holds the core state
and visible request pins during a DRAM wait, while CTC/PIO/scanner/controller
clocks continue. WAIT remains visible on the pin interface. This bus adapter
is not calibrated transistor-level Z80 T-state timing; vendor code is unchanged.
Machine copies/moves are disabled because its controller borrows its backing
vector objects.

DRAM value/known-mask bytes start unknown, independently of zero-initialized
host storage. CPU writes establish known bytes; direct writeMemory fixture
initialization also establishes them. A read of uninitialized DRAM stalls
rather than fabricating zero/random contents. Actual power-up randomness,
refresh retention and physical access/RC delays are unmodeled.

## Validation

bash tools/build-native.sh now builds thirteen test targets. New tests cover
explicit RAS row/CAS column and delayed IC103 edge order, actual Z80 write /
read / continue through all8 banks and16 startup phases, one commit per held
request, local ROM/SRAM bypass, unknown-read stalls and continued scanner /
controller clocks during main-CPU holds. The test's bank0 target is5234h,
outside the local ROM/SRAM overlay.

ALU address latches are staged, but backend service currently accepts only
resolved CPU-selected cycles matching the active CPU request. Full sample
port memory effects and DAC output must be added before calling this a full
DRAM controller or a playable sampler.


The source-only dram_probe accepts supplied PROM paths and verifies their
CPU write/read sequence across16 explicit phases/all8 banks with known quiet
CHRQ inputs. This is a characterized controller fixture, not full Machine
startup. No PROM bytes are embedded. Run:

    build/dram_probe ../roms/ic135.bin ../roms/ic137.bin

A separate synthetic Z80 self-check with the original PROMs deliberately
starts DRAM use before normal board initialization. Its result is recorded
separately; unknown startup inputs are not silently replaced with quiet ones.


## Correlated upper-PROM execution (0.16)

The 0.15 first-write stall was a loss of temporal correlation. Merging
UCD15/14 feedback bits admitted extra PROM addresses; merging UCD09 edges
then made the completion FF and next request pipeline unknown. A valid CPU
cycle could finish in every concrete history at different source clocks,
while the merged model could no longer establish that it had finished.

With CPU DRAM enabled and an upper PROM loaded, each of the sixteen startup
phase trajectories now retains a set of upper-controller histories. A history
contains the upper latch, captured UREQ, request/read pipeline, completion and
CPU WAIT flip-flops, delayed UCD08/UCD09, and its own DRAM port state. Unknown
UREQ branches at use of the sampled latch. PROM input feedback and request
completion always come from that same history. Observed consensus is never
fed back as the upper-controller state. Captures use old counter/shift/latch
values, and the request pipeline samples completion before its same-edge
update.

Equivalent histories coalesce only when all future-relevant state agrees;
DRAM diagnostic counters do not affect equivalence. Unknown bit representatives
are ignored in comparisons. A write commits only after every surviving
history in every startup phase has serviced the same address/data. Reads
likewise require IC103 data agreement and the existing WAIT/readiness guards.
No startup phase or quiet request level is selected to obtain progress.

The shared channel request/ACK network and lower-PROM/RFD/ALU path still use
conservative consensus. Thus this refinement preserves upper-control and CPU
DRAM history; it does not establish complete sample-path correlation or
physical timing calibration. Sample cycles still do not mutate CPU backing
memory. Uninitialized reads remain unknown and hold the CPU.

The independent upper-controller test enumerates concrete Boolean histories
from every initial feedback/pipeline value and both request levels. Across
192 source clocks, including active writes, reads, completion and request
release, its state set exactly equals the projected implementation states.
The fixture peaks at eight histories. A feedback-dependent priority PROM also
passes real-Z80 DRAM write/read/continue in every bank over all startup phases.
The external-image dram_probe now runs both quiet and unknown CHRQ fixtures
for every phase/bank; no firmware bytes are included in the source.


Original main2.1/scanner3.1 + both supplied PROMs reaches Insert Diskette at
40M CPU ticks in strict mode:7 DRAM writes,12 reads,95 WAIT hold clocks and
32 paired PIO transfers. The0.15 first-write stall is resolved. See
validation/dram-correlated-main21-scan31.txt and docs/VALIDATION.md.
Diagnostic4M HE TE remains Static RAM OK/help with no DRAM cycles or holds.
These results do not establish a complete OS boot or sample playback.


Checkpoint0.17 implements the playback read and stored DAC-code path described
in SAMPLE_TRANSPORT.md. Earlier CPU-only/sample-absent notes above describe
the original0.15/0.16 scope; CPU write commit/WAIT guards remain in use.
